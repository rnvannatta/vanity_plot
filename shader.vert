#version 450
layout(location = 0) in vec4 pos;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vs_color;

layout(push_constant) uniform PerDraw {
  mat4 mvp;
};

void main() {
  gl_Position = mvp * pos;
  vs_color = color;
}
