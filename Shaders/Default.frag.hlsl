struct PS_Input
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
};

float4 main(PS_Input input) : SV_Target0
{
    return input.color;
}
