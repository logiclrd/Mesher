#version 450
layout(location=0) noperspective in vec2 aaDistance;
layout(location=0) out vec4 outColor;
void main() {
    // One-physical-pixel alpha coverage ramp surrounding an opaque
    // centered stroke. The framebuffer-sized quad and noperspective
    // distance interpolation make its thickness consistent under orbit
    // and avoid thin, broken 1px Vulkan LINE_LIST diagonals.
    float coverage=clamp(aaDistance.y-abs(aaDistance.x),0.0,1.0);
    if(coverage<=0.0)discard;
    // RGB 30% display gray encoded as linear for the sRGB swapchain.
    outColor=vec4(0.073239,0.073239,0.073239,coverage);
}
