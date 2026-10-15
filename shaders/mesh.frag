#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 surfaceNormal;
layout(location=2) flat in float surfaceMaterial;
layout(push_constant) uniform Constants {
    mat4 viewProjection;
    vec4 light0;
    vec4 light1;
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
    outColor=vec4(min(albedo*intensity,vec3(1.0)),1.0);
}
