difficulty 'Sunshine'
stable_id 'sunshine'
resolution(960)
columns(4)
measure {id='intro', note {key='one', row=0, lane=1},
    note {key='two', row=480, lane=3}}
measure {id='outro', note {key='three', row=0, lane=2},
    note {key='four', row=480, lane=4}}
bpm {key='tempo', at=at_measure('intro',0), value=120}
