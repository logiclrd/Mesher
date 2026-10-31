#version 450
layout(location=0) flat in float kind;
layout(location=1) in vec4 paintColor;
layout(location=2) in vec2 atlasUV;
layout(set=0,binding=0) uniform sampler2D newModelPreviewAtlas;
layout(location=0) out vec4 outColor;
void main() {
    // Existing material: 0 bright text; 1 translucent backing.
    // M09: 2 green means both *plans* agree; 3 red means disagree.
    // Neither color means a repair is correct or passed M04.
    // Material 5: transparent PNG preview, sampled once from the packed
    // preview atlas. Material 6: hue-200 dialog panel/button/text colours.
    if(kind>5.5) {outColor=paintColor;return;}
    if(kind>4.5) {outColor=texture(newModelPreviewAtlas,atlasUV)*paintColor;return;}
    if(kind>3.5) {outColor=vec4(paintColor.rgb,1.0);return;}
    outColor = kind > 2.5
        ? vec4(1.0, 0.31, 0.31, 1.0)
        : kind > 1.5
        ? vec4(0.37, 1.0, 0.50, 1.0)
        : kind > 0.5
        ? vec4(0.005, 0.009, 0.015, 0.78)
        : vec4(0.92, 0.96, 1.0, 1.0);
}
