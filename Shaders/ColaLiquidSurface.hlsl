// M_BaseLiquid custom node. Original inputs and float2 output are preserved.
// Active bottles supply a damped world-space normal, wave energy and retained fluid spin.
// Resting ISMs supply a derived clipping offset; no per-instance motion tick or MID.
float3 upWS = normalize(InstanceUpWS);
float3 rightWS = normalize(InstanceRightWS - upWS * dot(InstanceRightWS, upWS));
float3 forwardWS = normalize(cross(upWS, rightWS));
float3 legacyTilt = float3(VelocityWS.xy * SloshStrength * 0.03, 0);
legacyTilt *= min(1.0, 0.35 / max(length(legacyTilt), 0.0001));
float3 normalWS = normalize(float3(legacyTilt.xy, 1));
if (NativeMotion > 0.5)
    normalWS = normalize(float3(NativeSurfaceNormalWS.xy, max(NativeSurfaceNormalWS.z, 0.6)));
float3 g = normalize(float3(dot(normalWS, rightWS), dot(normalWS, forwardWS), dot(normalWS, upWS)));

float radius = max(ContainerRadius, 0.01);
float halfHeight = max(ContainerHeight * 0.5, 0.01);
float3 localCenter = float3(0, 0, halfHeight * PivotAtBase);
// Fill and offset have already been resolved together in the vertex stage.
// Never mix a manually selected fill with an unrelated cached full-bottle plane.
float fill = saturate(LiquidLevel);
float projectedExtent = abs(g.z) * halfHeight + length(g.xy) * radius;
float offset = NativePlaneOffset;

float3 uWS = normalize(float3(1, 0, -normalWS.x / max(normalWS.z, 0.6)));
float3 vWS = cross(normalWS, uWS);
float3 uLocal = float3(dot(uWS, rightWS), dot(uWS, forwardWS), dot(uWS, upWS));
float3 vLocal = float3(dot(vWS, rightWS), dot(vWS, forwardWS), dot(vWS, upWS));
float3 p = LocalPos - localCenter;
float2 uv = float2(dot(p, uLocal), dot(p, vLocal));
float headroom = 4.0 * fill * (1.0 - fill);
float energy = NativeMotion > 0.5 ? saturate(MotionState.x) : saturate(length(VelocityWS.xy) * SloshStrength * 0.03);
// The existing angular parameter remains degrees/sec; native retained spin is radians/sec.
float spin = NativeMotion > 0.5 ? MotionState.y : dot(AngularVelocityWS, normalWS) * 0.0174532925;
spin *= clamp(AngularStrength / 0.02, 0.0, 3.0);
float displacement = 0;
float2 slope = 0;

[branch] if (headroom > 0.0001 && (energy > 0.0001 || abs(spin) > 0.02))
{
    float seed = MotionState.z;
    float k = clamp(abs(WaveFrequency), 0.005, 0.5);
    float speed = max(abs(WaveSpeed), 0.1);
    float a = dot(uv, float2(0.8, 0.6)) * k - Time * speed + seed;
    float b = dot(uv, float2(-0.385, 0.923)) * k * 1.37 - Time * speed * 1.17047 + seed * 2.31;
    float c = dot(uv, float2(0.174, -0.985)) * k * 2.11 - Time * speed * 1.45258 + seed * 4.17;
    float amplitude = min(abs(WaveAmplitude), radius * 0.12) * energy * sqrt(headroom);
    displacement = amplitude * (sin(a) * 0.58 + sin(b) * 0.28 + sin(c) * 0.14);
    slope = amplitude * k * (cos(a) * 0.58 * float2(0.8, 0.6)
        + cos(b) * 0.28 * 1.37 * float2(-0.385, 0.923)
        + cos(c) * 0.14 * 2.11 * float2(0.174, -0.985));
    // Centrifugal free surface: center dips, walls rise. Zero mean over a circular section.
    float alignment = abs(dot(normalWS, upWS));
    float vortexDepth = min(spin * spin * radius * radius / 1960.0 * clamp(VortexStrength / 0.03, 0.0, 4.0),
        min(radius * 0.4, halfHeight * 0.16) * headroom) * alignment * alignment;
    displacement += vortexDepth * (0.5 - dot(uv, uv) / (radius * radius));
    slope -= 2.0 * vortexDepth * uv / (radius * radius);
}

float distToPlane = dot(p, g) - offset + displacement;
float softness = max(EdgeSoftness, 0.001);
float mask = 1.0 - smoothstep(-softness, softness, distToPlane);
if (fill <= 0.0001) mask = 0;
if (fill >= 0.9999) mask = 1;
float depth = max(projectedExtent + offset, 0.001);
float gradient = saturate(1.0 + distToPlane / depth) * mask;

SurfaceNormalWS = normalize(normalWS + uWS * slope.x + vWS * slope.y);
SurfaceNormalLocal = g;
SurfaceDistance = distToPlane;
PlaneOffset = offset;
return float2(mask, gradient);
