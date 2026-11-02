#include "osbx_lua.h"
#include "lua_value_reader.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace wormedit::scripting {
namespace {
namespace values = lua_value_detail;
constexpr std::string_view declarations = R"lua(
local caller_location, native_simplify, native_bezier = ...
local type, pairs, ipairs, error, tostring = type, pairs, ipairs, error, tostring
local integer, huge = math.type, math.huge
local state = {nodes={}, images={}}
local scenes, scene_keys, project_images = {}, {}, {}
local selected, failure
local function where(skip)
  local file, line = caller_location(skip or 0)
  return {file=file, line=line}
end
local function malformed(message)
  local p=where()
  failure = failure or (p.file .. ':' .. p.line .. ': ' .. message)
  error(failure,0)
end
local function text(value, name)
  if type(value)~='string' or value=='' then malformed(name .. ' requires nonempty text') end
end
local function fields(value, allowed, name)
  if type(value)~='table' then malformed(name .. ' requires a table') end
  for k in pairs(value) do
    if not allowed[k] then malformed(name .. ': unknown field ' .. tostring(k)) end
  end
end
local function array(value, name)
  if type(value)~='table' then malformed(name .. ' requires a dense array') end
  local count=0
  for k in pairs(value) do
    if integer(k)~='integer' or k<1 then malformed(name .. ' requires a dense array') end
    count=count+1
  end
  for i=1,count do if value[i]==nil then malformed(name .. ' requires a dense array') end end
  return count
end
local function number(value,name)
  if type(value)~='number' or value~=value or value==huge or value==-huge then malformed(name .. ' requires finite numeric values') end
end
local function numeric(value,name)
  if type(value)=='number' then number(value,name); return {value} end
  array(value,name)
  local copy={}
  for i,v in ipairs(value) do number(v,name); copy[i]=v end
  return copy
end
local function current()
  if not selected then malformed('Declare group or sprite before object settings') end
  return selected
end
local function declared()
  if not state.id then malformed('Declare storyboard before settings or objects') end
end
function storyboard(id)
  text(id,'storyboard')
  if scene_keys[id] then malformed('Duplicate storyboard key: ' .. id) end
  if state.id then state={nodes={},images={}} end
  selected=nil
  scene_keys[id]=true
  scenes[#scenes+1]=state
  state.id=id
  state.provenance=where()
end
function api_version(value) declared(); text(value,'api_version'); state.api_version=value end
function canvas(value)
  declared(); fields(value,{width=true,height=true},'canvas')
  number(value.width,'canvas.width'); number(value.height,'canvas.height')
  state.canvas={width=value.width,height=value.height}
end
local function object(kind,id)
  declared(); text(id,kind)
  selected={kind=kind,id=id,ops={},provenance=where()}
  state.nodes[#state.nodes+1]=selected
end
function group(id) object('group',id) end
function sprite(id) object('sprite',id) end
function animation(id,settings)
  fields(settings,{file=true,frame_count=true,frame_delay_ms=true,loop_type=true},'animation')
  text(settings.file,'animation.file')
  if integer(settings.frame_count)~='integer' or settings.frame_count<1 or settings.frame_count>4294967295 then malformed('Invalid animation frame count') end
  number(settings.frame_delay_ms,'animation.frame_delay_ms')
  if settings.frame_delay_ms<=0 then malformed('Invalid animation delay') end
  local loop=settings.loop_type or 'LoopForever'
  if loop~='LoopForever' and loop~='LoopOnce' then malformed('Invalid animation loop type') end
  object('animation',id)
  local image_id=id .. '/animation-image'
  state.images[#state.images+1]={id=image_id,file=settings.file,provenance=where()}
  selected.image=image_id
  selected.animation={frame_count=settings.frame_count,frame_delay_ms=settings.frame_delay_ms,loop_type=loop}
  return {scope=state.id}
end
function sample(id,settings)
  fields(settings,{path=true,time_ms=true,volume=true},'sample')
  text(settings.path,'sample.path')
  if integer(settings.time_ms)~='integer' then malformed('Sample time requires integer milliseconds') end
  if settings.volume~=nil and (integer(settings.volume)~='integer' or settings.volume<1 or settings.volume>100) then malformed('Invalid sample volume') end
  object('sample',id)
  selected.sample={path=settings.path,time_ms=settings.time_ms,volume=settings.volume}
  return {scope=state.id}
end
function image(value)
  declared()
  if type(value)=='string' then
    text(value,'image')
    if current().animation then malformed('Complete Animation template is fixed after capture') end
    current().image=value; return
  end
  fields(value,{id=true,file=true},'image')
  text(value.id,'image.id'); text(value.file,'image.file')
  state.images[#state.images+1]={id=value.id,file=value.file,provenance=where()}
end
-- Consumed privately by the shared project runtime. It resolves/hash-captures
-- files before forwarding canonical image paths to this declaration owner.
function _wormedit_osbx_project_image(value)
  -- Project assets can precede the included storyboard declaration. If no
  -- storyboard is declared, the optional finisher leaves chart output alone.
  project_images[#project_images+1]={id=value.id,file=value.file,provenance=where(1)}
end
function layer(value) text(value,'layer'); current().layer=value end
function order(value)
  if integer(value)~='integer' then malformed('order requires an integer') end
  current().order=value
end
function parent(value) text(value,'parent'); current().parent=value end
function origin(value) text(value,'origin'); current().origin=value end
function key(value)
  fields(value,{[1]=true,value=true,easing=true,id=true},'key')
  if integer(value[1])~='integer' then malformed('key time requires integer milliseconds') end
  if value.id~=nil then text(value.id,'key.id') end
  if value.easing~=nil then
    if type(value.easing)=='table' then
      local controls=numeric(value.easing,'key.easing')
      if #controls~=4 then malformed('key.easing requires four bezier components') end
    elseif type(value.easing)=='number' then
      if integer(value.easing)~='integer' then malformed('key.easing requires an OSB integer') end
    elseif type(value.easing)~='string' then
      malformed('key.easing requires a name, OSB integer or cubic bezier array')
    end
  end
  local v=value.value
  if type(v)~='boolean' and type(v)~='string' then v=numeric(v,'key.value') end
  return {_key=true,time=value[1],value=v,easing=value.easing,id=value.id,provenance=where()}
end
-- Simplification selects existing linear descriptors; provenance and IDs survive.
keys = {}
function keys.bezier(options)
  fields(options,{from=true,to=true,points=true,tolerance=true},'keys.bezier')
  if integer(options.from)~='integer' or integer(options.to)~='integer' then malformed('keys.bezier requires integer from/to milliseconds') end
  array(options.points,'keys.bezier.points')
  if #options.points~=4 then malformed('keys.bezier requires four control points') end
  local dims=type(options.points[1])=='number' and 1 or 2
  local packed={}
  for _,point in ipairs(options.points) do
    local v=numeric(point,'keys.bezier point')
    if #v~=dims then malformed('keys.bezier point dimensions mismatch') end
    for d=1,dims do packed[#packed+1]=v[d] end
  end
  local tolerance=options.tolerance or 0.5
  number(tolerance,'keys.bezier.tolerance')
  if tolerance<=0 then malformed('keys.bezier.tolerance must be positive') end
  local baked=native_bezier(options.from,options.to,packed,dims,tolerance)
  local result={}
  for i=1,#baked,dims+1 do
    local value={}
    for d=1,dims do value[d]=baked[i+d] end
    result[#result+1]=key{baked[i],value=value,easing='linear'}
  end
  return result
end
function keys.simplify(options)
  fields(options,{keys=true,tolerance=true,dims=true},'keys.simplify')
  local input=options.keys
  array(input,'keys.simplify.keys')
  local dims=options.dims or 1
  if integer(dims)~='integer' or dims<1 or dims>3 then malformed('keys.simplify.dims must be 1, 2 or 3') end
  number(options.tolerance,'keys.simplify.tolerance')
  if options.tolerance<0 then malformed('keys.simplify.tolerance must be nonnegative') end
  local packed={}
  for i,k in ipairs(input) do
    if type(k)~='table' or not k._key then malformed('keys.simplify requires key descriptors') end
    if k.easing~=nil and k.easing~='linear' and k.easing~=0 then malformed('keys.simplify requires linear keys') end
    if #k.value~=dims then malformed('keys.simplify key dimensions mismatch') end
    packed[#packed+1]=k.time
    for d=1,dims do packed[#packed+1]=k.value[d] end
  end
  local selected=native_simplify(packed,dims,options.tolerance)
  local result={}
  for i,index in ipairs(selected) do result[i]=input[index] end
  return result
end
local function check_arity(value,name,arity)
  if #value~=arity then malformed(name .. ' requires ' .. arity .. ' components') end
end
local function temporal(value,kind)
  local allowed=kind=='loop' and {id=true,start=true,count=true,keys=true} or {id=true,name=true,from=true,to=true,keys=true}
  fields(value,allowed,kind)
  if value.id~=nil then text(value.id,kind..'.id') end
  local result={_program=kind,id=value.id,keys=value.keys,provenance=where()}
  array(result.keys,kind..'.keys')
  for _,k in ipairs(result.keys) do
    if type(k)~='table' or not k._key then malformed(kind..'.keys requires only key descriptors') end
  end
  if kind=='loop' then
    result.start=value.start or 0; result.count=value.count
    if integer(result.start)~='integer' or integer(result.count)~='integer' or result.count<1 or result.count>4294967295 then malformed('Invalid loop start/count') end
  else
    text(value.name,'trigger.name'); result.name=value.name; result.from=value.from; result.to=value.to
    if (value.from~=nil and integer(value.from)~='integer') or (value.to~=nil and integer(value.to)~='integer') then malformed('Trigger bounds require integer milliseconds') end
  end
  return result
end
function loop(value) return temporal(value,'loop') end
function trigger(value) return temporal(value,'trigger') end
local function track(value,name,arity)
  if type(value)=='number' then
    local v=numeric(value,name); check_arity(v,name,arity)
    return {default=v,keys={}}
  end
  array(value,name)
  if type(value[1])=='table' and (value[1]._key or value[1]._program) then
    local keys,program={},{}
    local function numeric_key(k)
      if type(k.value)~='table' then malformed(name..' requires numeric key values') end
      check_arity(k.value,name,arity)
    end
    for _,v in ipairs(value) do
      if type(v)~='table' then malformed(name..' requires key/program descriptors') end
      if v._key then numeric_key(v); keys[#keys+1]=v
      elseif v._program then
        for _,k in ipairs(v.keys) do numeric_key(k) end
        program[#program+1]=v
      else malformed(name..' requires key/program descriptors') end
    end
    return {keys=keys,program=program}
  end
  local v=numeric(value,name); check_arity(v,name,arity)
  return {default=v,keys={}}
end
local function clip_bindings(value)
  local result={}
  for i,v in ipairs(value or {}) do
    fields(v,{name=true,value=true},'clip binding'); text(v.name,'parameter name')
    result[i]={name=v.name,value=numeric(v.value,'parameter value')}
  end
  array(value or {},'clip bindings')
  return result
end
local function clip_use(value)
  fields(value,{id=true,clip=true,start=true,time_scale=true,parameters=true},'use_clip')
  text(value.clip,'use_clip.clip')
  if value.id~=nil then text(value.id,'use_clip.id') end
  local start=value.start or 0
  if integer(start)~='integer' then malformed('use_clip.start requires integer milliseconds') end
  local scale=value.time_scale or 1
  number(scale,'use_clip.time_scale')
  if scale<=0 then malformed('use_clip.time_scale must be positive') end
  return {id=value.id,clip=value.clip,start=start,time_scale=scale,parameters=clip_bindings(value.parameters),provenance=where()}
end
local function clip_track(value,arity,defaults)
  if type(value)=='number' or (type(value)=='table' and value[1]~=nil) then return track(value,'clip track',arity) end
  value=value or {}
  fields(value,{default=true,keys=true,parameter=true,program=true},'clip track')
  if value.parameter~=nil then text(value.parameter,'clip track.parameter') end
  local keys=value.keys or {}; array(keys,'clip keys')
  for _,k in ipairs(keys) do
    if type(k)~='table' or not k._key then malformed('clip tracks require key descriptors') end
    check_arity(k.value,'clip key',arity)
  end
  local initial=numeric(value.default or defaults,'clip default')
  check_arity(initial,'clip default',arity)
  local program=value.program or {}; array(program,'clip program')
  for _,v in ipairs(program) do
    if type(v)~='table' or not v._program then malformed('clip program requires loop/trigger descriptors') end
    for _,k in ipairs(v.keys) do check_arity(k.value,'clip program key',arity) end
  end
  return {default=initial,keys=keys,parameter=value.parameter,program=program}
end
local function discrete_array(value)
  array(value,'discrete track')
  local result={keys={},program={}}
  for _,v in ipairs(value) do
    if type(v)~='table' then malformed('Expected discrete key/program descriptor') end
    if v._key then
      if v.easing~=nil and v.easing~='step' then malformed('Discrete easing must be step') end
      result.keys[#result.keys+1]={id=v.id,time=v.time,value=v.value}
    elseif v._program then result.program[#result.program+1]=v
    else malformed('Expected discrete key/program descriptor') end
  end
  return result
end
local function clip_discrete(value,default,kind)
  local function checked(v)
    if type(v)~=kind or (kind=='string' and v~='normal' and v~='additive') then malformed('invalid clip discrete value') end
    return v
  end
  if type(value)~='table' then return {default=checked(value),keys={}} end
  if value[1]~=nil then value=discrete_array(value) end
  fields(value,{default=true,keys=true,program=true},'clip discrete track')
  local initial=value.default; if initial==nil then initial=default end
  local result={default=checked(initial),keys={}}
  local keys=value.keys or {}; array(keys,'clip discrete keys')
  for i,k in ipairs(keys) do
    fields(k,{id=true,time=true,value=true},'clip discrete key')
    if integer(k.time)~='integer' then malformed('clip key time requires integer milliseconds') end
    if k.id~=nil then text(k.id,'clip key.id') end
    result.keys[i]={id=k.id,time=k.time,value=checked(k.value)}
  end
  result.program=value.program or {}; array(result.program,'discrete program')
  for _,v in ipairs(result.program) do
    if type(v)~='table' or not v._program then malformed('Expected loop/trigger descriptor') end
    for _,k in ipairs(v.keys) do
      checked(k.value)
      if k.easing~=nil and k.easing~='step' then malformed('Discrete program easing must be step') end
    end
  end
  return result
end
local clip_defaults={translate={0,0},rotate={0},scale={1,1},skew={0,0},matrix={1,0,0,1,0,0},pivot={0,0}}
local function clip_operation(value,depth)
  if depth>128 then malformed('clip pivot nesting limit exceeded') end
  fields(value,{id=true,kind=true,track=true,children=true},'clip operation')
  local defaults=clip_defaults[value.kind]
  if not defaults then malformed('unknown clip transform kind') end
  if value.id~=nil then text(value.id,'clip operation.id') end
  local result={id=value.id,kind=value.kind,track=clip_track(value.track,#defaults,defaults),children={},provenance=where()}
  local children=value.children or {}; array(children,'clip children')
  for i,child in ipairs(children) do result.children[i]=clip_operation(child,depth+1) end
  return result
end
function clip(value)
  declared()
  fields(value,{id=true,ops=true,tracks=true,parameters=true,uses=true},'clip')
  text(value.id,'clip.id')
  local result={id=value.id,ops={},parameters={},uses={},provenance=where()}
  local ops=value.ops or {}; array(ops,'clip.ops')
  for i,op in ipairs(ops) do result.ops[i]=clip_operation(op,1) end
  local parameters=value.parameters or {}; array(parameters,'clip.parameters')
  for i,p in ipairs(parameters) do
    fields(p,{name=true,default=true},'clip parameter'); text(p.name,'parameter name')
    result.parameters[i]={name=p.name,default=numeric(p.default,'parameter default')}
  end
  local uses=value.uses or {}; array(uses,'clip.uses')
  for i,use in ipairs(uses) do result.uses[i]=clip_use(use) end
  local tracks=value.tracks or {}; fields(tracks,{opacity=true,color=true,visibility=true,blend=true},'clip.tracks')
  result.opacity=clip_track(tracks.opacity,1,{1})
  result.color=clip_track(tracks.color,4,{1,1,1,1})
  result.visibility=clip_discrete(tracks.visibility==nil and true or tracks.visibility,true,'boolean')
  if tracks.blend~=nil then result.blend=clip_discrete(tracks.blend,'normal','string') end
  state.clips=state.clips or {}; state.clips[#state.clips+1]=result
end
function use_clip(value)
  local target=current(); local use=clip_use(value)
  target.uses=target.uses or {}; target.uses[#target.uses+1]=use
end
transform={}
local function operation(kind,value)
  local target=current()
  local descriptor={kind=kind,track=track(value,'transform.'..kind,#clip_defaults[kind]),provenance=where()}
  target.ops[#target.ops+1]=descriptor
  return descriptor
end
function transform.translate(value) return operation('translate',value) end
function transform.rotate(value) return operation('rotate',value) end
function transform.scale(value) return operation('scale',value) end
function transform.skew(value) return operation('skew',value) end
function transform.matrix(value) return operation('matrix',value) end
function transform.pivot(value)
  fields(value,{[1]=true,[2]=true,ops=true},'transform.pivot')
  number(value[1],'pivot.x'); number(value[2],'pivot.y')
  local children=value.ops or {}
  array(children,'pivot.ops')
  local target=current()
  local remove={}
  for _,child in ipairs(children) do
    local found=false
    for _,op in ipairs(target.ops) do if op==child then found=true end end
    if not found or remove[child] then malformed('pivot.ops must contain distinct operations on the current object') end
    remove[child]=true
  end
  local kept={}
  for _,op in ipairs(target.ops) do if not remove[op] then kept[#kept+1]=op end end
  target.ops=kept
  local op=operation('pivot',{value[1],value[2]})
  op.children=children
  return op
end
function opacity(value) current().opacity=track(value,'opacity',1) end
function color(value) current().color=track(value,'color',4) end
function blend(value)
  if type(value)=='string' then text(value,'blend'); current().blend=value
  else current().blend=clip_discrete(value,'normal','string') end
end
function visibility(value) current().visibility=clip_discrete(value,true,'boolean') end
local function finish_osbx_batch()
  if failure then error(failure,0) end
  for _,scene in ipairs(scenes) do
    for _,image in ipairs(project_images) do scene.images[#scene.images+1]=image end
  end
  return scenes
end
local function finish_osbx_declarations(value)
  if failure then error(failure,0) end
  declared()
  if #scenes~=1 then malformed('Declare exactly one storyboard') end
  finish_osbx_batch()
  if value~=nil then
    if type(value)~='table' then malformed('Storyboard declarations require no return or empty generator arrays') end
    for name,items in pairs(value) do
      if name~='notes' and name~='layers' and name~='sv_points' and name~='sv_spans' then malformed('Unexpected output mixed with storyboard') end
      if array(items,name)~=0 then malformed('Loose generator output requires the combined generator host') end
    end
  end
  return state
end
)lua";

auto number(const values::node_t& node) -> double {
    const auto& scalar = values::scalar(node);
    if (const auto* value = std::get_if<double>(&scalar))
        return *value;
    if (const auto* value = std::get_if<std::int64_t>(&scalar))
        return static_cast<double>(*value);
    values::malformed("OSBX numeric value required.");
}
auto numbers(const values::node_t& node) -> std::vector<double> {
    const auto& table = values::children(node);
    values::dense(table);
    std::vector<double> result;
    for (const auto& item : table)
        result.push_back(number(item));
    return result;
}
auto provenance(const values::table_t& table) -> osbx_provenance_t {
    const auto& value = values::children(values::required(table, "provenance"));
    return {.file = values::text(values::required(value, "file")),
            .line = static_cast<std::size_t>(values::integer(values::required(value, "line")))};
}
auto location(const osbx_provenance_t& value) -> std::string {
    return value.file + ":" + std::to_string(value.line);
}
auto easing(const values::node_t& value) -> osbx_easing_t {
    if (std::holds_alternative<values::table_t>(value.value)) {
        const auto v = numbers(value);
        if (v.size() != 4)
            values::malformed("Cubic bezier easing needs four components.");
        return osbx_cubic_bezier_t{v[0], v[1], v[2], v[3]};
    }
    const auto& scalar = values::scalar(value);
    if (const auto* text = std::get_if<std::string>(&scalar)) {
        if (*text == "linear")
            return osbx_linear_easing_t{};
        if (*text == "step")
            return osbx_step_easing_t{};
        if (*text == "in_out_sine")
            return osbx_osb_easing_t{17};
        values::malformed("Unknown OSBX easing: " + *text);
    }
    const auto index = values::signed_integer(value);
    if (index < 0 || index > 34)
        values::malformed("OSB easing must be 0 through 34.");
    return osbx_osb_easing_t{static_cast<std::int32_t>(index)};
}
class decoder_t {
  public:
    auto decode(const values::table_t& table) -> osbx_document_t {
        osbx_document_t result;
        identity(values::text(values::required(table, "id")), provenance(table));
        if (const auto* version = values::field(table, "api_version"))
            result.api_version = values::text(*version);
        if (const auto* canvas = values::field(table, "canvas")) {
            const auto& v = values::children(*canvas);
            result.canvas = {number(values::required(v, "width")),
                             number(values::required(v, "height"))};
        }
        for (const auto& image : values::children(values::required(table, "images"))) {
            const auto& v = values::children(image);
            auto id = values::text(values::required(v, "id"));
            identity(id, provenance(v));
            result.images.push_back({id, values::text(values::required(v, "file"))});
        }
        if (const auto* clips = values::field(table, "clips"))
            for (const auto& entry : values::children(*clips)) result.clips.push_back(decode_clip(values::children(entry)));
        const auto& nodes = values::children(values::required(table, "nodes"));
        for (const auto& node : nodes)
            result.nodes.push_back(decode_node(values::children(node)));
        inherit_layers(result, nodes);
        validate(result, provenance(table));
        return result;
    }

  private:
    std::map<std::string, osbx_provenance_t> identities_;
    void validate(const osbx_document_t& document, const osbx_provenance_t& source) const {
        const auto validation = validate_osbx_document(document);
        if (validation.ok())
            return;
        std::string message;
        for (const auto& diagnostic : validation.diagnostics) {
            const auto found = identities_.find(diagnostic.location.id);
            const auto& p = found == identities_.end() ? source : found->second;
            message +=
                location(p) + ": " + diagnostic.location.id + ": " + diagnostic.message + "\n";
        }
        values::malformed(std::move(message));
    }
    void identity(const std::string& id, const osbx_provenance_t& p) {
        const auto [existing, inserted] = identities_.emplace(id, p);
        if (!inserted)
            values::malformed("Duplicate OSBX ID '" + id + "' at " + location(p) +
                              "; first declared at " + location(existing->second));
    }
    template<class Value, class Decode> auto program_keys(const values::table_t& value,
        const std::string& prefix, Decode decode) -> std::vector<osbx_key_t<Value>> {
        std::vector<osbx_key_t<Value>> result;
        for (const auto& entry : values::children(values::required(value, "keys"))) {
            const auto& key = values::children(entry);
            const auto id = values::field(key, "id") ? values::text(*values::field(key, "id")) : prefix + "/key/" + std::to_string(result.size() + 1);
            identity(id, provenance(key));
            osbx_key_t<Value> item{.id = id, .time_ms = values::signed_integer(values::required(key, "time")),
                .value = decode(values::required(key, "value"))};
            if constexpr (std::is_same_v<Value, std::vector<double>>) {
                if (const auto* e = values::field(key, "easing")) item.easing = easing(*e);
            } else item.easing = osbx_step_easing_t{};
            result.push_back(std::move(item));
        }
        return result;
    }
    template<class Value, class Decode> auto decode_programs(const values::table_t& value,
        const std::string& prefix, Decode decode) -> std::vector<osbx_key_program_t<Value>> {
        std::vector<osbx_key_program_t<Value>> result;
        const auto* input = values::field(value, "program");
        if (!input) return result;
        for (const auto& entry : values::children(*input)) {
            const auto& block = values::children(entry);
            const auto id = values::field(block, "id") ? values::text(*values::field(block, "id")) : prefix + "/program/" + std::to_string(result.size() + 1);
            identity(id, provenance(block));
            auto keys = program_keys<Value>(block, id, decode);
            const auto kind = values::text(values::required(block, "_program"));
            if (kind == "loop") {
                const auto count = values::integer(values::required(block, "count"));
                if (!count || count > UINT32_MAX) values::malformed("Loop count exceeds uint32");
                result.push_back({.data = osbx_loop_t<Value>{.id = id, .start_ms = values::signed_integer(values::required(block, "start")),
                    .count = static_cast<std::uint32_t>(count), .keys = std::move(keys)}});
            } else if (kind == "trigger") result.push_back({.data = decode_trigger<Value>(block, id, std::move(keys))});
            else values::malformed("Unknown temporal program descriptor");
        }
        return result;
    }
    template<class Value> auto decode_trigger(const values::table_t& value, const std::string& id,
        std::vector<osbx_key_t<Value>> keys) -> osbx_trigger_t<Value> {
        const auto name = storyboard_trigger_name_t::from(values::text(values::required(value, "name")));
        if (!name) values::malformed("Unsupported trigger name");
        storyboard_trigger_t header{.name = *name};
        if (const auto* field = values::field(value, "from")) header.start_time_ms = values::signed_integer(*field);
        if (const auto* field = values::field(value, "to")) header.end_time_ms = values::signed_integer(*field);
        if (!header.is_valid()) values::malformed("Invalid trigger window");
        return {.id = id, .trigger = std::move(header), .keys = std::move(keys)};
    }
    auto track(const values::table_t& value, std::vector<double> defaults,
               const std::string& prefix) -> osbx_track_t<> {
        osbx_track_t<> result{.default_value = std::move(defaults)};
        if (const auto* v = values::field(value, "default"))
            result.default_value = numbers(*v);
        if (const auto* v = values::field(value, "parameter")) result.parameter = values::text(*v);
        const auto& keys = values::children(values::required(value, "keys"));
        for (const auto& entry : keys) {
            const auto& v = values::children(entry);
            const auto p = provenance(v);
            try {
                const auto id = values::field(v, "id")
                                    ? values::text(*values::field(v, "id"))
                                    : prefix + "/key/" + std::to_string(result.keys.size() + 1);
                identity(id, p);
                osbx_key_t<> key{.id = id,
                                 .time_ms = values::signed_integer(values::required(v, "time")),
                                 .value = numbers(values::required(v, "value"))};
                if (const auto* e = values::field(v, "easing"))
                    key.easing = easing(*e);
                result.keys.push_back(std::move(key));
            } catch (const std::exception& e) {
                values::malformed(location(p) + ": " + e.what());
            }
        }
        result.program = decode_programs<std::vector<double>>(value, prefix, [](const auto& v) { return numbers(v); });
        return result;
    }
    auto operation(const values::table_t& value, const std::string& id, unsigned depth)
        -> osbx_transform_op_t {
        if (depth > 128)
            values::malformed("OSBX pivot nesting limit exceeded.");
        const auto name = values::text(values::required(value, "kind"));
        static const std::map<std::string, osbx_transform_kind_t> kinds{
            {"translate", osbx_transform_kind_t::translate},
            {"rotate", osbx_transform_kind_t::rotate},
            {"scale", osbx_transform_kind_t::scale},
            {"skew", osbx_transform_kind_t::skew},
            {"matrix", osbx_transform_kind_t::matrix},
            {"pivot", osbx_transform_kind_t::pivot}};
        const auto kind = kinds.at(name);
        std::vector<double> defaults{0, 0};
        if (kind == osbx_transform_kind_t::scale)
            defaults = {1, 1};
        if (kind == osbx_transform_kind_t::rotate)
            defaults = {0};
        if (kind == osbx_transform_kind_t::matrix)
            defaults = {1, 0, 0, 1, 0, 0};
        identity(id, provenance(value));
        osbx_transform_op_t result{
            .id = id,
            .kind = kind,
            .track = track(values::children(values::required(value, "track")), defaults, id)};
        if (const auto* children = values::field(value, "children"))
            for (const auto& child : values::children(*children))
                result.children.push_back(
                    operation(values::children(child),
                              values::field(values::children(child), "id") ? values::text(*values::field(values::children(child), "id")) :
                              id + "/op/" + std::to_string(result.children.size() + 1), depth + 1));
        return result;
    }
    auto decode_uses(const values::table_t& value, const std::string& prefix) -> std::vector<osbx_clip_instance_t> {
        std::vector<osbx_clip_instance_t> result;
        const auto* uses = values::field(value, "uses");
        if (!uses) return result;
        for (const auto& entry : values::children(*uses)) {
            const auto& v = values::children(entry);
            osbx_clip_instance_t use{.id = values::field(v, "id") ? values::text(*values::field(v, "id")) :
                prefix + "/use/" + std::to_string(result.size() + 1), .clip = values::text(values::required(v, "clip")),
                .start_ms = values::signed_integer(values::required(v, "start")),
                .time_scale = number(values::required(v, "time_scale"))};
            identity(use.id, provenance(v));
            for (const auto& parameter : values::children(values::required(v, "parameters"))) {
                const auto& binding = values::children(parameter);
                use.parameters.push_back({.name = values::text(values::required(binding, "name")),
                    .value = numbers(values::required(binding, "value"))});
            }
            result.push_back(std::move(use));
        }
        return result;
    }
    template<class Value, class Decode> auto discrete_clip_track(const values::table_t& value,
        const std::string& prefix, Decode decode) -> osbx_track_t<Value> {
        osbx_track_t<Value> result{.default_value = decode(values::required(value, "default"))};
        for (const auto& entry : values::children(values::required(value, "keys"))) {
            const auto& v = values::children(entry);
            const auto id = values::field(v, "id") ? values::text(*values::field(v, "id")) :
                prefix + "/key/" + std::to_string(result.keys.size() + 1);
            identity(id, {});
            result.keys.push_back({.id = id, .time_ms = values::signed_integer(values::required(v, "time")),
                .value = decode(values::required(v, "value")), .easing = osbx_step_easing_t{}});
        }
        result.program = decode_programs<Value>(value, prefix, decode);
        return result;
    }
    auto decode_clip(const values::table_t& value) -> osbx_clip_t {
        osbx_clip_t result{.id = values::text(values::required(value, "id"))};
        identity(result.id, provenance(value));
        for (const auto& entry : values::children(values::required(value, "ops"))) {
            const auto& v = values::children(entry);
            result.ops.push_back(operation(v, values::field(v, "id") ? values::text(*values::field(v, "id")) :
                result.id + "/op/" + std::to_string(result.ops.size() + 1), 1));
        }
        for (const auto& entry : values::children(values::required(value, "parameters"))) {
            const auto& v = values::children(entry);
            result.parameters.push_back({.name = values::text(values::required(v, "name")),
                .default_value = numbers(values::required(v, "default"))});
        }
        result.uses = decode_uses(value, result.id);
        result.tracks.opacity = track(values::children(values::required(value, "opacity")), {1}, result.id + "/opacity");
        result.tracks.color = track(values::children(values::required(value, "color")), {1,1,1,1}, result.id + "/color");
        result.tracks.visibility = discrete_clip_track<bool>(values::children(values::required(value, "visibility")),
            result.id + "/visibility", [](const auto& v) { return std::get<bool>(values::scalar(v)); });
        if (const auto* v = values::field(value, "blend"))
            result.tracks.blend = discrete_clip_track<osbx_blend_t>(values::children(*v), result.id + "/blend",
                [](const auto& entry) {
                    const auto name = values::text(entry);
                    if (name == "normal") return osbx_blend_t::normal;
                    if (name == "additive") return osbx_blend_t::additive;
                    values::malformed("Unknown clip blend");
                });
        return result;
    }
    static auto blend_value(const values::node_t& item) -> osbx_blend_t {
        const auto name = values::text(item);
        if (name == "normal") return osbx_blend_t::normal;
        if (name == "additive") return osbx_blend_t::additive;
        values::malformed("Invalid blend program value");
    }
    void decode_node_discrete(osbx_node_t& result, const values::table_t& value) {
        if (const auto* v = values::field(value, "visibility"))
            result.tracks.visibility = discrete_clip_track<bool>(values::children(*v), result.id + "/visibility",
                [](const auto& item) { return std::get<bool>(values::scalar(item)); });
        if (const auto* v = values::field(value, "blend")) {
            if (std::holds_alternative<values::table_t>(v->value))
                result.tracks.blend = discrete_clip_track<osbx_blend_t>(values::children(*v), result.id + "/blend", blend_value);
            else {
                const auto name = values::text(*v);
                if (name != "normal" && name != "additive") values::malformed("OSBX blend must be normal or additive.");
                result.tracks.blend.default_value = name == "normal" ? osbx_blend_t::normal : osbx_blend_t::additive;
            }
        }
    }
    static auto node_kind(const std::string& name) -> osbx_node_kind_t {
        if (name == "group") return osbx_node_kind_t::group;
        if (name == "sprite") return osbx_node_kind_t::sprite;
        if (name == "animation") return osbx_node_kind_t::animation;
        if (name == "sample") return osbx_node_kind_t::sample;
        values::malformed("Unknown OSBX node kind");
    }
    static void decode_media(osbx_node_t& node, const values::table_t& value) {
        if (const auto* v = values::field(value, "animation")) {
            const auto& metadata = values::children(*v);
            const auto count = values::signed_integer(values::required(metadata, "frame_count"));
            if (count < 1 || count > UINT32_MAX) values::malformed("Invalid Animation frame count");
            node.animation = storyboard_animation_t{.frame_count = static_cast<std::uint32_t>(count),
                .frame_delay_ms = number(values::required(metadata, "frame_delay_ms")),
                .loop_type = values::text(values::required(metadata, "loop_type")) == "LoopOnce" ?
                    storyboard_animation_loop_t::loop_once : storyboard_animation_loop_t::loop_forever};
        }
        if (const auto* v = values::field(value, "sample")) {
            const auto& metadata = values::children(*v);
            node.sample = osbx_sample_t{.time_ms = values::signed_integer(values::required(metadata, "time_ms")),
                .path = values::text(values::required(metadata, "path"))};
            if (const auto* volume = values::field(metadata, "volume")) {
                const auto n = values::signed_integer(*volume);
                if (n < 1 || n > 100) values::malformed("Invalid Sample volume");
                node.sample->volume = static_cast<std::uint8_t>(n);
            }
        }
    }
    auto decode_node(const values::table_t& value) -> osbx_node_t {
        osbx_node_t result{.id = values::text(values::required(value, "id")),
                           .provenance = provenance(value)};
        identity(result.id, result.provenance);
        try {
            result.kind = node_kind(values::text(values::required(value, "kind")));
            decode_media(result, value);
            result.uses = decode_uses(value, result.id);
            if (const auto* v = values::field(value, "parent"))
                result.parent = values::text(*v);
            if (const auto* v = values::field(value, "image"))
                result.image = values::text(*v);
            if (const auto* v = values::field(value, "layer"))
                result.layer = layer(values::text(*v));
            if (const auto* v = values::field(value, "order")) {
                const auto n = values::signed_integer(*v);
                if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
                    values::malformed("OSBX order exceeds integer range.");
                result.order = static_cast<int>(n);
            }
            if (const auto* v = values::field(value, "origin"))
                result.origin = values::text(*v) == "centre" ? "Centre" : values::text(*v);
            for (const auto& op : values::children(values::required(value, "ops")))
                result.ops.push_back(
                    operation(values::children(op),
                              result.id + "/op/" + std::to_string(result.ops.size() + 1), 1));
            if (const auto* v = values::field(value, "opacity"))
                result.tracks.opacity = track(values::children(*v), {1}, result.id + "/opacity");
            if (const auto* v = values::field(value, "color"))
                result.tracks.color =
                    track(values::children(*v), {1, 1, 1, 1}, result.id + "/color");
            decode_node_discrete(result, value);
        } catch (const std::exception& e) {
            values::malformed(location(result.provenance) + ": " + e.what());
        }
        return result;
    }
    static auto layer(const std::string& name) -> osbx_layer_t {
        static const std::map<std::string, osbx_layer_t> layers{
            {"background", osbx_layer_t::background},
            {"fail", osbx_layer_t::fail},
            {"pass", osbx_layer_t::pass},
            {"foreground", osbx_layer_t::foreground}};
        if (const auto it = layers.find(name); it != layers.end())
            return it->second;
        values::malformed("Unknown OSBX layer: " + name);
    }
    static void inherit_layers(osbx_document_t& document, const values::table_t& descriptors) {
        std::map<std::string, std::size_t> indices;
        for (std::size_t i = 0; i < document.nodes.size(); ++i)
            indices.emplace(document.nodes[i].id, i);
        for (std::size_t i = 0; i < document.nodes.size(); ++i) {
            auto index = i;
            std::set<std::size_t> seen;
            while (!values::field(values::children(descriptors[index]), "layer") &&
                   document.nodes[index].parent && seen.insert(index).second) {
                const auto found = indices.find(*document.nodes[index].parent);
                if (found == indices.end())
                    break;
                index = found->second;
            }
            document.nodes[i].layer = document.nodes[index].layer;
        }
    }
};
} // namespace

auto osbx_lua_prelude() -> std::string_view {
    return declarations;
}
auto decode_lua_osbx(const lua_sandbox_context_t& table) -> osbx_document_t {
    return decoder_t{}.decode(table);
}
auto decode_lua_storyboards(const lua_sandbox_context_t& scenes) -> std::vector<generator_storyboard_t> {
    values::dense(scenes);
    if (scenes.size() > maximum_generator_storyboards) values::malformed("Generator storyboard limit exceeded.");
    std::vector<generator_storyboard_t> output;
    std::set<std::string> keys;
    for (const auto& scene : scenes) {
        const auto& descriptor = values::children(scene);
        auto key = values::text(values::required(descriptor, "id"));
        if (!keys.insert(key).second) values::malformed("Duplicate storyboard key: " + key);
        output.push_back({.key = std::move(key), .value = decode_lua_osbx(descriptor)});
    }
    return output;
}
auto run_osbx_lua(std::string_view source, std::string_view chunk_name, lua_sandbox_limits_t limits,
                  const lua_sandbox_context_t& context) -> diagnosed_t<osbx_document_t> {
    try {
        auto first = lua_sandbox_t::from(limits);
        if (!first)
            values::malformed(first.error().message);
        if (auto installed = first->install_project_runtime(); !installed)
            values::malformed(installed.error().message);
        if (auto installed = first->install_snapshot("generator", context); !installed)
            values::malformed(installed.error().message);
        const auto adapter = std::string(declarations) + "\nreturn finish_osbx_declarations";
        const auto result = first->evaluate_context(source, chunk_name, adapter);
        if (!result)
            values::malformed(result.error().message);
        auto document = decode_lua_osbx(*result);
        limits.maximum_instructions = first->usage().remaining_instructions;
        limits.maximum_memory_bytes -= first->usage().allocated_bytes;
        auto replay = lua_sandbox_t::from(limits);
        if (!replay)
            values::malformed(replay.error().message);
        if (auto installed = replay->install_project_runtime(); !installed)
            values::malformed(installed.error().message);
        if (auto installed = replay->install_snapshot("generator", context); !installed)
            values::malformed(installed.error().message);
        const auto repeated = replay->evaluate_context(source, chunk_name, adapter);
        if (!repeated)
            values::malformed("OSBX replay failed: " + repeated.error().message);
        if (document != decode_lua_osbx(*repeated))
            values::malformed(
                "OSBX declarations are not deterministic for the supplied seed and context.");
        return {.value = std::move(document)};
    } catch (const std::exception& e) {
        return {.diagnostics = {error(diagnostic_code_t::invalid_binding, e.what())}};
    }
}
} // namespace wormedit::scripting
