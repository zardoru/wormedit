storyboard "Main"
  api_version "0.1"
  canvas { width = 640, height = 480 }

image { id = "logo", file = "sb/logo.png" }

group "title_group"
  layer "foreground"
  transform.translate { 320, 240 }
  transform.rotate {
    key { 0, value = 0, easing = "linear" },
    key { 2000, value = 360, easing = "in_out_sine" },
  }

sprite "title"
  parent "title_group"
  image "logo"
  origin "centre"
  transform.matrix {
    key { 0, value = { 1, 0, 0, 1, 0, 0 } },
    key { 2000, value = { 1, 0.2, 0, 1, 0, 0 } },
  }
  opacity {
    key { 0, value = 0 },
    key { 250, value = 1 },
  }
