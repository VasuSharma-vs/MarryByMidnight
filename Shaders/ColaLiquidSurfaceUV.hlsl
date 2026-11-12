// Existing float3 contract: RG = planar UV, B = visible liquid-surface foam mask.
// Use the same instance axes as the fill plane. A generic pixel-stage
// World->Local transform can resolve to the ISM component, not this bottle.
float3 upWS = normalize(InstanceUpWS);
float3 rightWS = normalize(InstanceRightWS-upWS*dot(InstanceRightWS,upWS));
float3 forwardWS = normalize(cross(upWS,rightWS));
float3 V = normalize(float3(dot(CameraVectorWS,rightWS),dot(CameraVectorWS,forwardWS),dot(CameraVectorWS,upWS)));
float3 N = normalize(LocalGravity);
float3 center = float3(0, 0, ContainerHeight * 0.5 * PivotAtBase);
float3 planeCenter = center + N * NativePlaneOffset;
float distance = dot(planeCenter - LocalPos, N);
float denominator = dot(N, V);

// A camera ray directed away from the surface cannot display its foam.
// abs(NdotV) incorrectly projected these rays into the liquid and painted walls.
float validSurface = (denominator > 0.02 && distance >= -0.05) ? 1.0 : 0.0;
float t = max(distance, 0.0) / max(denominator, 0.02);
float maxDistance = max(ContainerHeight, ContainerRadius * 2.0) * 3.0;
validSurface *= t <= maxDistance ? 1.0 : 0.0;
float3 hitPos = LocalPos + V * min(t, maxDistance);

float s = N.z >= 0.0 ? 1.0 : -1.0;
float a = -1.0 / (s + N.z);
float b = N.x * N.y * a;
float3 uAxis = float3(1.0 + s*N.x*N.x*a, s*b, -s*N.x);
float3 vAxis = float3(b, s + N.y*N.y*a, -N.y);
float3 rel = hitPos - planeCenter;
float2 p = float2(dot(rel, uAxis), dot(rel, vAxis));
float radius = max(ContainerRadius, 0.001);
float2 planeUV = p / (radius * 2.0) * Tiling + 0.5 + PanSpeed * Time;
float foamMask = (1.0 - smoothstep(radius * 0.85, radius, length(p))) * validSurface;
return float3(planeUV, foamMask);
