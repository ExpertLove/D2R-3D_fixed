"""windows_resource: compile a .rc file with rc.exe and link the .res into dependents.
"""

load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _windows_resource_impl(ctx):
    cc_toolchain = find_cc_toolchain(ctx)
    features = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    env = cc_common.get_environment_variables(
        feature_configuration = features,
        action_name = ACTION_NAMES.cpp_compile,
        variables = cc_common.empty_variables(),
    )

    comp = cc_common.merge_compilation_contexts(
        compilation_contexts = [d[CcInfo].compilation_context for d in ctx.attr.deps],
    )
    res = ctx.actions.declare_file(ctx.label.name + ".res")

    args = ctx.actions.args()
    args.add_all(["/c", "rc.exe", "/nologo"])
    args.add_all(ctx.attr.defines, format_each = "/d%s")
    args.add_all(depset(transitive = [comp.includes, comp.quote_includes, comp.system_includes]), before_each = "/i")
    args.add("/fo", res)
    args.add(ctx.file.src)

    ctx.actions.run(
        executable = env.get("ComSpec", "C:/Windows/System32/cmd.exe"),
        arguments = [args],
        inputs = depset([ctx.file.src], transitive = [comp.headers]),
        outputs = [res],
        env = env,
        mnemonic = "WindowsResource",
        progress_message = "Compiling resource %{input}",
    )

    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        user_link_flags = [res.path],
        additional_inputs = depset([res]),
    )
    return [
        DefaultInfo(files = depset([res])),
        CcInfo(linking_context = cc_common.create_linking_context(
            linker_inputs = depset([linker_input]),
        )),
    ]

windows_resource = rule(
    implementation = _windows_resource_impl,
    attrs = {
        "src": attr.label(allow_single_file = [".rc"], mandatory = True),
        "deps": attr.label_list(providers = [CcInfo], doc = "cc_library targets providing headers/includes for the .rc."),
        "defines": attr.string_list(),
    },
    toolchains = use_cc_toolchain(),
    fragments = ["cpp"],
)
