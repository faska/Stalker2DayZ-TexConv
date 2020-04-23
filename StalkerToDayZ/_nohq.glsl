#version 130

uniform vec2      textureSize;
uniform sampler2D bumpTexture;
out vec4 outputTexture;

void main()
{
	vec2 normalizedCoords = vec2(gl_FragCoord.x, textureSize.y - gl_FragCoord.y)/textureSize.xy;
	vec4 temp = texture(bumpTexture, normalizedCoords.xy);

	outputTexture.rgb = temp.abg;
}
