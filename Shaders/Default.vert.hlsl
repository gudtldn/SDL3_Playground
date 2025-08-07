struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;

    // 변환 없이 직접 전달 (NDC 좌표계)
    output.position = float4(input.position, 1.0);

    // 색상을 프래그먼트 셰이더로 전달
    output.color = input.color;

    return output;
}
