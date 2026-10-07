local target = lito.target({ kind = "lib", name = "wavsen" })
local environment = lito.target_preprocessor_environment(target)
local video = false
for _, definition in ipairs(environment.definitions) do
    if definition == "LITO_FEAT_VIDEO=1" then video = true end
end
if not video then return end

local tool = lito.tool("glslang")

lito.run({
    tool = tool,
    cwd = ".",
    args = { "-V", "--vn", "nv12_to_rgba_spv", "-o", "@OUTPUT:1@", "@INPUT:1@" },
    inputs = { "shaders/nv12_to_rgba.comp" },
    outputs = { "include/nv12_to_rgba.spv.h" },
})

lito.target_add_generated_include(target, "include")

lito.write({
    output = "include/nv12_to_bgra.metal.h",
    inputs = { "src/video/apple/nv12_to_bgra.metal" },
    content = [[static constexpr unsigned char wavsen_nv12_metal[] = {
#embed "@INPUT:1@"
, 0
};
]],
})
