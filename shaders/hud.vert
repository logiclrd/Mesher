#version 450
layout(location=0) in vec3 position;
layout(location=2) in float material;
layout(location=0) flat out float kind;
void main() {
    // Screen-space, already in Vulkan clip coordinates.
    gl_Position = vec4(position, 1.0);
    kind = material;
}
