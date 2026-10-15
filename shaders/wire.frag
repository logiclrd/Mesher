#version 450
layout(location=0) out vec4 outColor;
void main() { // RGB 30% display gray encoded as linear for an sRGB swapchain.
    outColor=vec4(0.073239,0.073239,0.073239,1.0); }
