#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in float material;
layout(push_constant) uniform Constants {
    mat4 viewProjection;
    vec4 light0;
    vec4 light1;
} constants;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 surfaceNormal;
layout(location=2) flat out float surfaceMaterial;
void main() {
    worldPosition=position;
    surfaceNormal=normal;
    surfaceMaterial=material;
    gl_Position=constants.viewProjection*vec4(position,1.0);
}
