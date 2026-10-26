--- A complete generated difficulty, runnable directly or included from project.lua.
difficulty 'Easy'
stable_id 'easy'
resolution(960)
columns(4)
mode 'dance-single'
offset(measure_time '-0.125')
measure {id='intro', length=measure_length '0.75', meter=meter(6,8),
    note {key='first', row=0, lane=1},
    hold {key='long', row=240, lane=2, end_at=at_measure('outro',0)}}
measure {id='outro', note {key='last', row=0, lane=4}}
bpm {key='tempo', at=at_measure('intro',0), value=150}
stop {key='pause', at=at_measure('intro',120), length=rows(60)}
scroll {key='reverse', at=at_measure('outro',0), value=ratio(-1,2)}
