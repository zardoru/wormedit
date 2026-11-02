-- A separate scene keeps the main edition exportable.
storyboard 'Banner'
image {id='banner-image', file='assets/banner.png'}
sprite 'banner'
image 'banner-image'
transform.rotate {-4}
-- The generated operation ID is banner/op/2; osu! has no shear command.
transform.skew {12,0}
transform.translate {300,390}
opacity {key {0,value=0}, key {600,value=1}, key {4000,value=1}}
