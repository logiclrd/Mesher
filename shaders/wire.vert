#version 450
layout(location=0) in vec3 position;
layout(push_constant) uniform Constants {
    mat4 viewProjection;
    vec4 light0;
    vec4 light1;
} constants;
void main() {
    gl_Position=constants.viewProjection*vec4(position,1.0);
    // Preserve EXACT solid-triangle clip depth. The old global -0.00012*w
    // bias displaced every wire toward the eye by thousands of depth
    // representable steps, exposing wire edges through nearer solid faces.
    // The wire pipeline uses LEQUAL and never writes depth; hidden edges
    // must be occluded by the solid depth buffer, including at joins.
}
