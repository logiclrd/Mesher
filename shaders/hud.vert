#version 450
layout(location=0) in vec3 position;
layout(location=2) in float material;
layout(location=3) in vec4 vertexColor;
layout(location=0) flat out float kind;
layout(location=1) out vec4 paintColor;
layout(location=2) out vec2 atlasUV;
layout(location=1) in vec3 inputNormal;
void main() {
    // Screen-space, already in Vulkan clip coordinates.
    gl_Position = vec4(position, 1.0);
    kind = material;
    paintColor = vertexColor;
    atlasUV = inputNormal.xy;
}
