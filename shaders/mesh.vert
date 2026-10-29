#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in float material;
layout(location=3) in vec4 vertexColor;
layout(push_constant) uniform Constants {
    mat4 viewProjection;
    vec4 light0;
    vec4 light1;
    vec4 wireRaster;
} constants;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 surfaceNormal;
layout(location=2) flat out float surfaceMaterial;
layout(location=3) noperspective out vec3 edgeBarycentric;
layout(location=4) out vec4 paintColor;
void main() {
    worldPosition=position;
    surfaceNormal=normal;
    surfaceMaterial=material;
    paintColor=vertexColor;
    int corner=gl_VertexIndex%3;
    edgeBarycentric=corner==0?vec3(1,0,0):corner==1?vec3(0,1,0):vec3(0,0,1);
    gl_Position=constants.viewProjection*vec4(position,1.0);
}
