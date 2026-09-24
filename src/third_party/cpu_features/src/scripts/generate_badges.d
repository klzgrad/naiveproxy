#!/usr/bin/env -S docker run --rm -v ${PWD}/scripts:/scripts -v ${PWD}/.github/workflows:/.github/workflows dlanguage/dmd dmd -run /scripts/generate_badges.d

// To run this script:
// cd /path/to/cpu_features
// ./scripts/generate_badges.d

import std.algorithm : each, map, cartesianProduct, filter, joiner, sort, uniq;
import std.array;
import std.base64 : Base64;
import std.conv : to;
import std.file : exists;
import std.format;
import std.range : chain, only;
import std.stdio;
import std.string : representation;
import std.traits : EnumMembers;

immutable string bazel_svg = `<svg role="img" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path d="M6 .16l5.786 5.786L6 11.732.214 5.946 6 .161zM0 6.214V12l5.786 5.786V12L0 6.214zM18 .16l5.786 5.786L18 11.732l-5.786-5.786L18 .161zM24 6.214V12l-5.786 5.786V12L24 6.214zM12 6.16l5.786 5.786L12 17.732l-5.786-5.786L12 6.161zM11.84 18.054v5.785l-5.786-5.785v-5.786l5.785 5.786zM12.16 18.054l5.786-5.786v5.786l-5.785 5.785v-5.785z" stroke="transparent" fill="white"/></svg>`;
const string bazel_svg_base64 = Base64.encode(representation(bazel_svg));

immutable string zig_svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 153 140"><g fill="white"><g><polygon points="46,22 28,44 19,30"/><polygon points="46,22 33,33 28,44 22,44 22,95 31,95 20,100 12,117 0,117 0,22" shape-rendering="crispEdges"/><polygon points="31,95 12,117 4,106"/></g><g><polygon points="56,22 62,36 37,44"/><polygon points="56,22 111,22 111,44 37,44 56,32" shape-rendering="crispEdges"/><polygon points="116,95 97,117 90,104"/><polygon points="116,95 100,104 97,117 42,117 42,95" shape-rendering="crispEdges"/><polygon points="150,0 52,117 3,140 101,22"/></g><g><polygon points="141,22 140,40 122,45"/><polygon points="153,22 153,117 106,117 120,105 125,95 131,95 131,45 122,45 132,36 141,22" shape-rendering="crispEdges"/><polygon points="125,95 130,110 106,117"/></g></g></svg>`;
const string zig_svg_base64 = Base64.encode(representation(zig_svg));

enum BuildSystem
{
    CMake,
    Bazel,
    Zig
}

enum Cpu
{
    amd64,
    AArch64,
    ARM,
    MIPS,
    POWER,
    RISCV,
    s390x,
}

enum Os
{
    Linux,
    MacOS,
    Windows,
}

struct Badge
{
const:

    Cpu cpu;
    Os os;
    BuildSystem build_system;

private:
    string id()
    {
        return format("%d%c%d", cast(uint)(os) + 1, cast(char)('a' + cpu),
            cast(uint)(build_system));
    }

    string link_ref()
    {
        return format("[l%s]", id());
    }

    string image_ref()
    {
        return format("[i%s]", id());
    }

    string filename()
    {
        import std.uni : toLower;

        return toLower(format("%s_%s_%s.yml", cpu, os, build_system));
    }

    bool enabled()
    {
        return exists("../.github/workflows/" ~ filename());
    }

    string append_logo(string url)
    {
        final switch (build_system)
        {
        case BuildSystem.CMake:
            return url ~ "&logo=cmake";
        case BuildSystem.Bazel:
            return url ~ "&logo=data:image/svg%2bxml;base64," ~ bazel_svg_base64;
        case BuildSystem.Zig:
            return url ~ "&logo=data:image/svg%2bxml;base64," ~ zig_svg_base64;
        }
    }

public:

    string disabled_image_ref()
    {
        return format("[d%d]", cast(uint)(build_system));
    }

    string text()
    {
        if (enabled())
            return format("[![%s]%s]%s", build_system, image_ref, link_ref);
        return format("![%s]%s", build_system, disabled_image_ref);
    }

    string disabled_image_link()
    {
        return append_logo(format("%s: https://img.shields.io/badge/n%%2Fa-lightgrey?",
                disabled_image_ref));
    }

    string link_decl()
    {
        return format("%s: https://github.com/google/cpu_features/actions/workflows/%s",
            link_ref, filename());
    }

    string image_decl()
    {
        return append_logo(format("%s: https://img.shields.io/github/actions/workflow/status/google/cpu_features/%s?branch=main&event=push&label=",
                image_ref, filename()));
    }

}

auto tableHeader(in Os[] oses)
{
    return chain(only(""), oses.map!(to!string)).array;
}

auto tableAlignment(in Os[] oses)
{
    return chain(only(":--"), oses.map!(v => "--:")).array;
}

auto tableCell(Range)(in Os os, in Cpu cpu, Range badges)
{
    return badges.filter!(b => b.cpu == cpu && b.os == os)
        .map!(b => b.text())
        .joiner("<br/>").to!string;
}

auto tableRow(Range)(in Cpu cpu, in Os[] oses, Range badges)
{
    return chain(only(cpu.to!string), oses.map!(os => tableCell(os, cpu, badges))).array;
}

auto tableRows(Range)(in Os[] oses, in Cpu[] cpus, Range badges)
{
    return cpus.map!(cpu => tableRow(cpu, oses, badges)).array;
}

auto table(Range)(in Os[] oses, in Cpu[] cpus, Range badges)
{
    return chain(only(tableHeader(oses)), only(tableAlignment(oses)),
        tableRows(oses, cpus, badges));
}

void main()
{
    immutable allCpus = [EnumMembers!Cpu];
    immutable allOses = [EnumMembers!Os];
    immutable allBuildSystems = [EnumMembers!BuildSystem];
    auto badges = cartesianProduct(allCpus, allOses, allBuildSystems).map!(
        t => Badge(t[0], t[1], t[2]));
    writefln("%(|%-( %s |%) |\n%) |", table(allOses, allCpus, badges));
    writeln();
    badges.filter!(b => !b.enabled)
        .map!(b => b.disabled_image_link())
        .array
        .sort
        .uniq
        .each!writeln;
    badges.filter!(b => b.enabled)
        .map!(b => [b.link_decl(), b.image_decl()])
        .joiner().array.sort.uniq.each!writeln;
}
