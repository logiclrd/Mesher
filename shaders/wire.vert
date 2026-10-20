#version 450
layout(location=0) in vec3 position;      // immutable edge endpoint A
layout(location=1) in vec3 otherEndpoint; // immutable edge endpoint B
layout(location=2) in float cornerIndex;  // 0,1,2,0,2,3
layout(push_constant) uniform Constants {
    mat4 viewProjection;
    vec4 light0;
    vec4 light1;
    vec4 wireRaster; // framebuffer width, height, quad outer halfwidth px, unused
} constants;
layout(location=0) noperspective out vec2 aaDistance;

void main() {
    vec4 start=constants.viewProjection*vec4(position,1.0);
    vec4 end=constants.viewProjection*vec4(otherEndpoint,1.0);
    // Vulkan near-clip z>=0. Clip the segment endpoints before computing
    // screen-space tangents: projecting a behind-camera endpoint otherwise
    // produces a many-screen-width streak.
    if(start.z<0.0 && end.z<0.0) {
        gl_Position=vec4(0.0,0.0,-1.0,1.0);
        aaDistance=vec2(0.0);
        return;
    }
    const float clipEpsilon=0.000001;
    if(start.z<clipEpsilon)
        start=mix(start,end,clamp((clipEpsilon-start.z)/
                  max(end.z-start.z,clipEpsilon),0.0,1.0));
    if(end.z<clipEpsilon)
        end=mix(end,start,clamp((clipEpsilon-end.z)/
                max(start.z-end.z,clipEpsilon),0.0,1.0));

    vec2 framebuffer=constants.wireRaster.xy;
    float halfWidth=constants.wireRaster.z;
    if(min(start.w,end.w)<=clipEpsilon ||
       framebuffer.x<=0.0||framebuffer.y<=0.0) {
        gl_Position=vec4(0.0,0.0,-1.0,1.0);
        aaDistance=vec2(0.0);
        return;
    }
    vec2 diff=(end.xy/end.w-start.xy/start.w)*framebuffer*0.5;
    float lengthPx=length(diff);
    if(lengthPx<0.0001) {
        gl_Position=vec4(0.0,0.0,-1.0,1.0);
        aaDistance=vec2(0.0);
        return;
    }
    vec2 perpendicular=vec2(-diff.y,diff.x)/lengthPx;
    int corner=int(round(cornerIndex));
    bool atEnd=corner>=2;
    float side=(corner==0||corner==3)?-1.0:1.0;
    vec4 projected=atEnd?end:start;
    // Offset only x/y in screen pixels, *never* move clip z:
    // existing solid depth writes and LEQUAL wire test still occlude
    // rear/internal fragments. M56's depth pull must not return.
    projected.xy+=perpendicular*(side*halfWidth)*
                  (2.0/framebuffer)*projected.w;
    gl_Position=projected;
    aaDistance=vec2(side*halfWidth,halfWidth);
}
