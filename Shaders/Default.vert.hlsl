cbuffer UBO : register(b0, space1)
{
    float4x4 MVP;
}

struct VertexInput
{
    float4 position : POSITION;
    float3 normal : NORMAL;
    float2 tex_coord : TEXCOORD;
    float4 tangent : TANGENT;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(MVP, input.position);
    output.normal = input.normal;
    return output;
}
