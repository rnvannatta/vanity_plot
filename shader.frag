#version 450
layout(location = 0) in vec4 vs_color;
layout(location = 0) out vec4 fs_color;
void main() {
  fs_color = vs_color;
}
