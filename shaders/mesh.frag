#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 surfaceNormal;
layout(location=2) flat in float surfaceMaterial;
layout(location=3) noperspective in vec3 edgeBarycentric;
layout(push_constant) uniform Constants {
    mat4 viewProjection;
    vec4 light0;
    vec4 light1;
    vec4 wireRaster;
} constants;
layout(location=0) out vec4 outColor;
void main() {
    vec3 n=normalize(surfaceNormal);
    vec3 lightA=constants.light0.xyz-worldPosition;
    vec3 lightB=constants.light1.xyz-worldPosition;
    float diffuseA=max(dot(n,normalize(lightA)),0.0);
    float diffuseB=max(dot(n,normalize(lightB)),0.0);
    float intensity=0.19+0.69*diffuseA+0.44*diffuseB;
    vec3 albedo=surfaceMaterial>0.5 ? vec3(0.25,0.27,0.30) : vec3(0.63,0.67,0.72);
    vec3 color=min(albedo*intensity,vec3(1.0));
    // Shade mesh wires ONLY on the depth-visible owning solid triangle.
    // noperspective barycentric gradient converts to physical pixel
    // distance. Neither detached edge quads nor forward clip-Z bias.
    if(constants.wireRaster.w>0.5) {
        float distancePx=1000000.0;
        if(surfaceMaterial<0.5) {
            // M67: only owning visible mesh triangle edges.
            vec3 dx=dFdx(edgeBarycentric);
            vec3 dy=dFdy(edgeBarycentric);
            vec3 perPixel=max(sqrt(dx*dx+dy*dy),vec3(0.0000001));
            vec3 distances=abs(edgeBarycentric)/perPixel;
            distancePx=min(distances.x,min(distances.y,distances.z));
        } else {
            // M68: unit XY grid lies ON the real visible floor plane.
            // Floor triangles are just two huge cells: do not draw their
            // false diagonal! Each cell boundary is an integer world
            // coordinate. Convert its local perspective-correct
            // world-space gradient into PHYSICAL framebuffer pixels.
            vec2 nearest=abs(worldPosition.xy-round(worldPosition.xy));
            vec2 unitsPerPixel=max(
                vec2(length(vec2(dFdx(worldPosition.x),dFdy(worldPosition.x))),
                     length(vec2(dFdx(worldPosition.y),dFdy(worldPosition.y)))),
                vec2(0.0000001));
            vec2 gridDistance=nearest/unitsPerPixel;
            distancePx=min(gridDistance.x,gridDistance.y);
        }
        float coverage=smoothstep(0.0,1.0,
                                  constants.wireRaster.z-distancePx);
        color=mix(color,vec3(0.073239),coverage);
    }
    outColor=vec4(color,1.0);
}
