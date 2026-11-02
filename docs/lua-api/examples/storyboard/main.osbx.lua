storyboard 'Sunshine'
api_version '0.1'
canvas {width=640, height=480}
image {id='sky-image', file='assets/sky.png'}
image {id='sun-image', file='assets/sun.png'}
image {id='rays-image', file='assets/rays.png'}
image {id='worm-image', file='assets/worm.png'}

sprite 'sky'
image 'sky-image'
layer 'background'
transform.translate {320,240}

group 'sun-turn'
layer 'background'
transform.rotate {key {0,value=0}, key {4000,value=180}}
transform.translate {480,110}

sprite 'rays'
parent 'sun-turn'
layer 'background'
image 'rays-image'
opacity {key {0,value=0}, key {400,value=0.9}, key {4000,value=0.9}}

sprite 'sun'
layer 'background'
image 'sun-image'
transform.translate {480,110}

group 'worm-turn'
transform.rotate {key {0,value=-15}, key {4000,value=45,easing='in_out_sine'}}
transform.translate {300,270}
opacity {key {0,value=0}, key {400,value=1}, key {3600,value=1}, key {4000,value=0}}

-- An offset under a rotating parent traces an arc and requires bounded baking.
sprite 'worm'
parent 'worm-turn'
image 'worm-image'
transform.translate {65,0}
color {key {0,value={1,1,1,1}}, key {4000,value={0.6,1,0.8,1}}}
