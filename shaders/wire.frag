#version 450
layout(location=0) noperspective in vec2 aaDistance;
layout(location=0) out vec4 outColor;
void main() {
    // M62: physically narrow, smooth one-pixel shoulder around the
    // original centerline; preserve M58 noperspective interpolation.
    // Occlusion by a nearer adjacent face can correctly clip one side.
    // Never recover that side by moving clip Z toward the viewer.
    float coverage=smoothstep(0.0,1.0,aaDistance.y-abs(aaDistance.x));
    if(coverage<=0.0)discard;
    // RGB 30% display gray encoded as linear for the sRGB swapchain.
    outColor=vec4(0.073239,0.073239,0.073239,coverage);
}
