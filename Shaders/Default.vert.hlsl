cbuffer UBO : register(b0, space1)
{
    float4x4 MVP;
}

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 tex_coord : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(MVP, float4(input.position, 1.0f));
    output.normal = input.normal;
    return output;
}