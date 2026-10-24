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
    // R01 one-shot inspection overlay uses unlit colors; this is independent
    // of ordinary material/wireframe appearance and cannot tint mesh data.
    if(surfaceMaterial>=1.5) {
        vec3 diagnostic;
        if(surfaceMaterial>=14.5)diagnostic=vec3(0.05,0.94,0.92); // M16D latest indexed edit
        else if(surfaceMaterial>=13.5)diagnostic=vec3(1.0,0.25,0.65); // M16D previous indexed edit
        else if(surfaceMaterial>=12.5)diagnostic=vec3(1.0,0.81,0.02); // M13 actual shared seam edge IDs, no geometry offset
        else if(surfaceMaterial>=11.5)diagnostic=vec3(0.05,0.08,0.11); // cap/zipper triangulation edges
        else if(surfaceMaterial>=10.5)diagnostic=vec3(0.97,0.09,0.13); // failed triangle edges
        else if(surfaceMaterial>=9.5)diagnostic=vec3(1.0,0.54,0.57); // tinted failed triangle faces
        else if(surfaceMaterial>=8.5)diagnostic=vec3(0.08,0.91,0.19); // R05 real green cap faces
        else if(surfaceMaterial>=7.5)diagnostic=vec3(0.10,0.42,1.0); // R04 actual new zipper faces
        else if(surfaceMaterial>=6.5)diagnostic=vec3(0.08,0.88,0.94); // added boundary vertex
        else if(surfaceMaterial>=5.5)diagnostic=vec3(1.0,0.83,0.07); // directed contour
        else if(surfaceMaterial>=4.5)diagnostic=vec3(0.75,0.13,0.84); // uncertain V14 face
        else if(surfaceMaterial>=3.5)diagnostic=vec3(1.0,0.43,0.075); // proposed excision
        else if(surfaceMaterial>=2.5)diagnostic=vec3(0.55,0.045,0.065); // R01 edges
        else diagnostic=vec3(1.0,0.08,0.09); // R01 incident vertices
        // M11A3d: inspection edges are shaded on their actual triangles
        // using per-pixel barycentric derivatives, independently of W.
        // M13 material 13 exact ID-shared seam markers stay distinct.
        if((surfaceMaterial>=7.5&&surfaceMaterial<10.5)||
           (surfaceMaterial>=13.5&&surfaceMaterial<15.5)) {
            vec3 dx=dFdx(edgeBarycentric);
            vec3 dy=dFdy(edgeBarycentric);
            vec3 perPixel=max(sqrt(dx*dx+dy*dy),vec3(0.0000001));
            vec3 distances=abs(edgeBarycentric)/perPixel;
            float distancePx=min(distances.x,min(distances.y,distances.z));
            float coverage=1.0-smoothstep(0.55,1.30,distancePx);
            vec3 normalEdge=vec3(0.05,0.08,0.11);
            vec3 warningEdge=vec3(0.97,0.09,0.13);
            diagnostic=mix(diagnostic,
                surfaceMaterial>=9.5?warningEdge:normalEdge,coverage);
        }
        outColor=vec4(diagnostic,1.0);
        return;
    }
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
