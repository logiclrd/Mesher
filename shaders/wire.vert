#version 450
layout(location=0) in vec3 position;
layout(push_constant) uniform Constants {
    mat4 viewProjection;
    vec4 light0;
    vec4 light1;
} constants;
void main() {
    gl_Position=constants.viewProjection*vec4(position,1.0);
    // Pull wire slightly toward the viewer to avoid coplanar depth fighting.
    gl_Position.z-=0.00012*gl_Position.w;
}
