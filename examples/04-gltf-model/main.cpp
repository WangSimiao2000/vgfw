#include "vgfw.hpp"

#include <chrono>

const char* vertexShaderSource = R"(
#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

layout(location = 0) out vec2 vTexCoords;
layout(location = 1) out vec3 vFragPos;
layout(location = 2) out vec3 vNormal;

layout(location = 0) uniform mat4 model;
layout(location = 1) uniform mat4 view;
layout(location = 2) uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vTexCoords = aTexCoords;
    vFragPos = vec3(model * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(model))) * aNormal;
}
)";

const char* fragmentShaderSource = R"(
#version 450

layout(location = 0) in vec2 vTexCoords;
layout(location = 1) in vec3 vFragPos;
layout(location = 2) in vec3 vNormal;

layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D baseColor;
layout(binding = 1) uniform sampler2D metallicRoughness;

layout(location = 3) uniform vec3 lightPos;
layout(location = 4) uniform vec3 viewPos;
layout(location = 5) uniform vec3 lightColor;
layout(location = 6) uniform vec3 objectColor;


// Cook-Torrance GGX (Trowbridge-Reitz) Distribution
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.1415926535897932384626433832795 * denom * denom;

    return num / max(denom, 0.001); // prevent divide by zero for roughness=0.0 and NdotH=1.0
}

float GeometrySmith_GGX(float NdotX, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float num = NdotX;
    float denom = NdotX * (1.0 - a) + a;

    return num / denom;
}

// Smith's GGX Visibility Function (Schlick-Beckmann approximation)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySmith_GGX(NdotV, roughness);
    float ggx1 = GeometrySmith_GGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Schlick's approximation for the Fresnel term
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void main()
{
    vec2 uv = vec2(vTexCoords.x, 1.0 - vTexCoords.y);

    // Retrieve material properties from metallicRoughness texture
    vec4 texSample = texture(metallicRoughness, uv);
    float metallic = texSample.b;
    float roughness = texSample.g;

    // Ambient
    vec3 ambient = lightColor * 0.03;

    // Diffuse
    vec3 norm = normalize(vNormal); // Use vertex normal directly
    vec3 lightDir = normalize(lightPos - vFragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Specular (Cook-Torrance BRDF)
    vec3 viewDir = normalize(viewPos - vFragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    float NDF = DistributionGGX(norm, halfwayDir, roughness);
    float G = GeometrySmith(norm, viewDir, lightDir, roughness);
    vec3 F0 = vec3(0.04); // default specular reflectance
    vec3 F = fresnelSchlick(max(dot(halfwayDir, viewDir), 0.0), F0);
    vec3 specular = (NDF * G * F) / (4.0 * max(dot(norm, viewDir), 0.0) * max(dot(norm, lightDir), 0.0));

    // Combine ambient, diffuse, and specular components
    vec3 result = (ambient + (1.0 - metallic) * diffuse + metallic * specular) * objectColor;

    // Output final color with baseColor texture
    FragColor = texture(baseColor, uv) * vec4(result, 1.0);
}
)";

int main()
{
    // Init VGFW
    if (!vgfw::init())
    {
        std::cerr << "Failed to initialize VGFW" << std::endl;
        return -1;
    }

    // Create a window instance
    auto window = vgfw::window::create({.Title = "04-gltf-model", .EnableMSAA = true, .AASample = 8});

    // Init renderer
    vgfw::renderer::init({.Window = window});

    // Get graphics & render context
    auto& rc = vgfw::renderer::getRenderContext();

    // Build vertex format
    auto vertexFormat = vgfw::renderer::VertexFormat::Builder {}.BuildDefault();

    // Get vertex array object
    auto vao = rc.GetVertexArray(vertexFormat->GetAttributes());

    // Create shader program
    auto program = rc.CreateGraphicsProgram(vertexShaderSource, fragmentShaderSource);

    // Build a graphics pipeline
    auto graphicsPipeline = vgfw::renderer::GraphicsPipeline::Builder {}
                                .SetDepthStencil({
                                    .DepthTest      = true,
                                    .DepthWrite     = true,
                                    .DepthCompareOp = vgfw::renderer::CompareOp::Less,
                                })
                                .SetRasterizerState({
                                    .PolygonMode = vgfw::renderer::PolygonMode::Fill,
                                    .CullMode    = vgfw::renderer::CullMode::Back,
                                    .ScissorTest = false,
                                })
                                .SetVAO(vao)
                                .SetShaderProgram(program)
                                .Build();

    // Load model
    vgfw::resource::Model suzanneModel {};
    if (!vgfw::io::load("assets/models/Suzanne.gltf", suzanneModel))
    {
        return -1;
    }

    // Get textures
    auto* baseColorTexture =
        suzanneModel.TextureMap[suzanneModel.MaterialMap[suzanneModel.Meshes[0].MaterialIndex].BaseColorTextureIndex];
    auto* metallicRoughnessTexture =
        suzanneModel.TextureMap[suzanneModel.MaterialMap[suzanneModel.Meshes[0].MaterialIndex].BaseColorTextureIndex];

    // Create index buffer & vertex buffer
    auto indexBuffer  = rc.CreateIndexBuffer(vgfw::renderer::IndexType::UInt32,
                                            suzanneModel.Meshes[0].Indices.size(),
                                            suzanneModel.Meshes[0].Indices.data());
    auto vertexBuffer = rc.CreateVertexBuffer(
        vertexFormat->GetStride(), suzanneModel.Meshes[0].Vertices.size(), suzanneModel.Meshes[0].Vertices.data());

    // Start time
    auto startTime = std::chrono::high_resolution_clock::now();

    // Camera properties
    float     fov = 60.0f;
    glm::vec3 viewPos(0.0f, 0.0f, 3.0f);

    // Light properties
    glm::vec3 lightPos(1.2f, 1.0f, 2.0f);
    glm::vec3 lightColor(1.0f, 1.0f, 1.0f);
    glm::vec3 objectColor(1.0f, 1.0f, 1.0f);

    // Main loop
    while (!window->ShouldClose())
    {
        window->OnTick();

        // Calculate the elapsed time
        auto  currentTime = std::chrono::high_resolution_clock::now();
        float time        = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        // Create the model matrix
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.5f, 1.0f, 0.0f));

        // Create the view matrix
        glm::mat4 view = glm::lookAt(viewPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        // Create the projection matrix
        glm::mat4 projection =
            glm::perspective(glm::radians(fov), window->GetWidth() * 1.0f / window->GetHeight(), 0.1f, 100.0f);

        // Render
        rc.BeginRendering({.Extent = {.Width = window->GetWidth(), .Height = window->GetHeight()}},
                          glm::vec4 {0.2f, 0.3f, 0.3f, 1.0f},
                          1.0f);
        rc.BindGraphicsPipeline(graphicsPipeline)
            .SetUniformMat4("model", model)
            .SetUniformMat4("view", view)
            .SetUniformMat4("projection", projection)
            .SetUniformVec3("lightPos", lightPos)
            .SetUniformVec3("viewPos", viewPos)
            .SetUniformVec3("lightColor", lightColor)
            .SetUniformVec3("objectColor", objectColor)
            .BindTexture(0, *baseColorTexture)
            .BindTexture(1, *metallicRoughnessTexture)
            .Draw(vertexBuffer,
                  indexBuffer,
                  suzanneModel.Meshes[0].Indices.size(),
                  suzanneModel.Meshes[0].Vertices.size());

        vgfw::renderer::beginImGui();
        ImGui::Begin("GLTF Model");
        ImGui::SliderFloat("Camera FOV", &fov, 1.0f, 179.0f);
        ImGui::DragFloat3("Camera Position", glm::value_ptr(viewPos));
        ImGui::DragFloat3("Light Position", glm::value_ptr(lightPos));
        ImGui::ColorEdit3("Light Color", glm::value_ptr(lightColor));
        ImGui::ColorEdit3("Object Color", glm::value_ptr(objectColor));
        ImGui::End();
        vgfw::renderer::endImGui();

        vgfw::renderer::present();
    }

    // Cleanup
    rc.Destroy(indexBuffer);
    rc.Destroy(vertexBuffer);
    vgfw::shutdown();

    return 0;
}