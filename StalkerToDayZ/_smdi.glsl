#version 130

uniform vec2      textureSize;
uniform sampler2D bumpTexture;
uniform sampler2D bumpSharpTexture;
out vec4 outputTexture;

void main()
{
	vec2 normalizedCoords = vec2(gl_FragCoord.x, textureSize.y - gl_FragCoord.y)/textureSize.xy;
	vec4 temp = texture(bumpTexture, normalizedCoords.xy);
	
	outputTexture.rgb = vec3(temp.gr,texture(bumpSharpTexture, normalizedCoords.xy).a);
}
