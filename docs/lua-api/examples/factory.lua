--- Select entry point "rise" and add numeric parameter "height".
return {
    rise = function(ctx)
        local height = ctx.parameters.height
        return function(p, ctx)
            return ctx.baseline + p * height
        end
    end
}
