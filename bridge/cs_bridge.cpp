// preFlight implementation of the Cubby Slicer engine C ABI
// (cubby-slicer/docs/ENGINE-CONTRACT.md). Engine-neutral job parsing, bounds
// checking and output buffers live in wasm-bridge/cs_common.hpp.
//
// Rules this file follows:
//  * no exception ever escapes an exported function (std::exception and ...
//    are caught and turned into rc != 0 + report);
//  * every engine object (Model, Print, GCodeProcessorResult) is scoped to one
//    call, so repeated slices on one instance start from a clean state;
//  * no global operator new/delete replacement, no function-pointer casts.

#include "cs_common.hpp"

#include <libslic3r/libslic3r.h>
#include "libslic3r_version.h" // generated into the libslic3r binary dir (a public include dir)
#include <libslic3r/BuildVolume.hpp>
#include <libslic3r/MultipleBeds.hpp>
#include <libslic3r/Exception.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/OrcaGCodeAliases.hpp>
#include <libslic3r/PlaceholderParser.hpp>
#include <libslic3r/Preset.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/Utils.hpp>
#include <libslic3r/GCode/GCodeObject.hpp>
#include <libslic3r/GCode/GCodeProcessor.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

#ifndef CS_BUILD_VARIANT
#define CS_BUILD_VARIANT "release"
#endif

using namespace Slic3r;
using cs::json;

namespace {

// ------------------------------------------------------------- one-time init
void ensure_init()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    // Errors only; Boost.Log's default sink writes to stderr (Module.printErr).
    set_logging_level(1);
}

// -------------------------------------------------------------- config apply
// Serialize a JSON value for `def` the way that option's deserialize() parses it.
// Returns false when the JSON shape cannot represent the option type.
bool json_to_native(const ConfigOptionDef &def, const json &v, std::string &out)
{
    if (!v.is_array()) {
        if (v.is_object())
            return false;
        out = cs::scalar_to_string(v);
        return true;
    }
    if (def.is_scalar()) {
        // Point / Point3 may arrive as [x, y(, z)].
        if (def.type == coPoint || def.type == coPoint3) {
            size_t n = def.type == coPoint ? 2 : 3;
            if (v.size() != n)
                return false;
            std::string s;
            for (size_t i = 0; i < n; ++i) {
                if (!v[i].is_number())
                    return false;
                if (i)
                    s += 'x';
                s += cs::scalar_to_string(v[i]);
            }
            out = s;
            return true;
        }
        // A one-element array for a scalar option is accepted as that element.
        if (v.size() == 1 && !v[0].is_array() && !v[0].is_object()) {
            out = cs::scalar_to_string(v[0]);
            return true;
        }
        return false;
    }
    if (def.type == coStrings) {
        // ConfigOptionStrings::deserialize == unescape_strings_cstyle (';'-separated, C-style quoting).
        std::vector<std::string> strs;
        strs.reserve(v.size());
        for (const json &e : v) {
            if (e.is_array() || e.is_object())
                return false;
            strs.emplace_back(cs::scalar_to_string(e));
        }
        out = escape_strings_cstyle(strs);
        return true;
    }
    // Numeric / bool / percent / enum / point vectors: ','-separated; points are "XxY".
    std::string s;
    bool first = true;
    for (const json &e : v) {
        std::string item;
        if (def.type == coPoints && e.is_array()) {
            if (e.size() != 2 || !e[0].is_number() || !e[1].is_number())
                return false;
            item = cs::scalar_to_string(e[0]) + "x" + cs::scalar_to_string(e[1]);
        } else if (e.is_array() || e.is_object()) {
            return false;
        } else {
            item = cs::scalar_to_string(e);
        }
        if (!first)
            s += ',';
        s += item;
        first = false;
    }
    out = s;
    return true;
}

struct ApplyLog {
    std::vector<std::string> unknown;   // keys the engine does not know
    json substitutions = json::array(); // "key: old -> new"
};

std::string describe_value(const json &v)
{
    std::string s = v.is_string() ? v.get<std::string>() : v.dump();
    if (s.size() > 200)
        s = s.substr(0, 200) + "...";
    return s;
}

// Apply {"key": value} onto `cfg` (a DynamicPrintConfig or ModelConfig) with
// recorded substitution of values the engine cannot parse.
template <typename Cfg>
void apply_json_config(Cfg &cfg, const json &obj, ApplyLog &log, const std::string &prefix = "")
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        std::string key = it.key();
        // Legacy renames are resolved by set_deserialize() (handle_legacy); look up the
        // definition after that mapping so e.g. old PrusaSlicer keys still work.
        std::string legacy_key = key, legacy_val;
        PrintConfigDef::handle_legacy(legacy_key, legacy_val);
        if (legacy_key.empty()) {
            // handle_legacy() drops obsolete AND unknown keys; the engine would ignore them silently.
            log.unknown.push_back(prefix + key);
            continue;
        }
        const ConfigOptionDef *def = print_config_def.get(legacy_key);
        if (def == nullptr) {
            log.unknown.push_back(prefix + key);
            continue;
        }
        std::string value;
        if (!json_to_native(*def, it.value(), value)) {
            log.substitutions.push_back(prefix + key + ": " + describe_value(it.value()) +
                                        " -> (ignored: JSON shape does not match option type)");
            continue;
        }
        // "Enable" = substitute unknown enum/bool values with the default AND record them
        // (preFlight's EnableSilent substitutes without recording).
        ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Enable);
        try {
            cfg.set_deserialize(key, value, ctx);
        } catch (const UnknownOptionException &) {
            log.unknown.push_back(prefix + key);
            continue;
        } catch (const std::exception &e) {
            log.substitutions.push_back(prefix + key + ": " + describe_value(it.value()) + " -> (ignored: " +
                                        e.what() + ")");
            continue;
        }
        for (const ConfigSubstitution &sub : ctx.substitutions) {
            std::string nv = sub.new_value ? sub.new_value->serialize() : std::string("default");
            std::string k = sub.opt_def ? sub.opt_def->opt_key : key;
            log.substitutions.push_back(prefix + k + ": " + sub.old_value + " -> " + nv);
        }
    }
}

DynamicPrintConfig build_print_config(const json &config_json, ApplyLog &log)
{
    DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
    apply_json_config(cfg, config_json, log);
    cfg.handle_legacy_composite();
    cfg.normalize_fdm();
    return cfg;
}

void add_warning(json &report, const std::string &code, const std::string &message,
                 std::unordered_set<std::string> *seen = nullptr)
{
    if (seen && !seen->insert(code + '\x1f' + message).second)
        return;
    report["warnings"].push_back({{"code", code}, {"message", message}});
}

void report_apply_log(json &report, const ApplyLog &log)
{
    for (const json &s : log.substitutions)
        report["substitutions"].push_back(s);
    if (!log.unknown.empty()) {
        std::string msg = "Ignored " + std::to_string(log.unknown.size()) + " option(s) unknown to (or obsolete in) preFlight: ";
        for (size_t i = 0; i < log.unknown.size(); ++i) {
            if (i >= 40) {
                msg += ", ...";
                break;
            }
            if (i)
                msg += ", ";
            msg += log.unknown[i];
        }
        add_warning(report, "unknown_options", msg);
    }
}

// ------------------------------------------------------------------- model
// Mesh-local vertices → bed coordinates. The host already flips the winding
// of mirrored transforms (see ENGINE-CONTRACT.md), so indices are used as
// given; an inside-out result is corrected via the signed volume.
TriangleMesh make_mesh(std::vector<float> &positions, std::vector<uint32_t> &indices, const double *t, const std::string &name)
{
    const size_t nv = positions.size() / 3;
    indexed_triangle_set its;
    its.vertices.reserve(nv);
    for (size_t i = 0; i < nv; ++i) {
        const double x = positions[3 * i], y = positions[3 * i + 1], z = positions[3 * i + 2];
        const double X = t[0] * x + t[4] * y + t[8] * z + t[12];
        const double Y = t[1] * x + t[5] * y + t[9] * z + t[13];
        const double Z = t[2] * x + t[6] * y + t[10] * z + t[14];
        const float fx = float(X), fy = float(Y), fz = float(Z);
        if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz))
            throw cs::JobError(name + ": transformed vertex is not finite");
        its.vertices.emplace_back(fx, fy, fz);
    }
    const double det = t[0] * (t[5] * t[10] - t[9] * t[6]) - t[4] * (t[1] * t[10] - t[9] * t[2]) +
                       t[8] * (t[1] * t[6] - t[5] * t[2]);
    if (!(std::abs(det) > 1e-12))
        throw cs::JobError(name + ": transform is singular");
    const size_t nt = indices.size() / 3;
    its.indices.reserve(nt);
    for (size_t i = 0; i < nt; ++i) {
        its.indices.emplace_back(int(indices[3 * i]), int(indices[3 * i + 1]), int(indices[3 * i + 2]));
    }
    // The input is no longer needed; free it before the engine allocates.
    std::vector<float>().swap(positions);
    std::vector<uint32_t>().swap(indices);

    its_remove_degenerate_faces(its);
    if (its.indices.empty())
        throw cs::JobError(name + ": mesh has no non-degenerate triangles");
    its_compactify_vertices(its);
    TriangleMesh mesh(std::move(its));
    if (mesh.volume() < 0)
        mesh.flip_triangles();
    return mesh;
}

// ------------------------------------------------------------------ stats
json collect_stats(const Print &print, const GCodeProcessorResult &result, const DynamicPrintConfig &cfg)
{
    json stats;
    const auto &ps = result.print_statistics;
    stats["printTimeSec"] = double(ps.modes[size_t(PrintEstimatedStatistics::ETimeMode::Normal)].time);

    const auto *diam = cfg.option<ConfigOptionFloats>("filament_diameter");
    const auto *dens = cfg.option<ConfigOptionFloats>("filament_density");
    const auto *cost = cfg.option<ConfigOptionFloats>("filament_cost");
    size_t n = 0;
    for (const auto &kv : ps.volumes_per_extruder)
        n = std::max(n, kv.first + 1);
    if (n == 0)
        n = 1;
    json mm = json::array(), cm3 = json::array(), g = json::array(), money = json::array();
    for (size_t e = 0; e < n; ++e) {
        auto it = ps.volumes_per_extruder.find(e);
        const double vol = it == ps.volumes_per_extruder.end() ? 0. : it->second; // mm^3
        const double d = diam && !diam->values.empty() ? diam->get_at(e) : 1.75;
        const double rho = dens && !dens->values.empty() ? dens->get_at(e) : 0.;
        const double c = cost && !cost->values.empty() ? cost->get_at(e) : 0.;
        const double area = PI * 0.25 * d * d;
        const double grams = vol * rho * 0.001;
        mm.push_back(area > 0 ? vol / area : 0.);
        cm3.push_back(vol * 0.001);
        g.push_back(grams);
        money.push_back(grams * c * 0.001);
    }
    stats["filamentMm"] = mm;
    stats["filamentCm3"] = cm3;
    stats["filamentG"] = g;
    stats["filamentCost"] = money;

    std::set<long long> zs;
    double max_z = 0.;
    for (const PrintObject *po : print.objects()) {
        for (const Layer *l : po->layers()) {
            zs.insert(llround(l->print_z * 1e5));
            max_z = std::max(max_z, double(l->print_z));
        }
        for (const SupportLayer *l : po->support_layers()) {
            zs.insert(llround(l->print_z * 1e5));
            max_z = std::max(max_z, double(l->print_z));
        }
    }
    stats["layers"] = zs.size();
    stats["maxZ"] = max_z;
    return stats;
}

void collect_step_warnings(const Print &print, json &report, std::unordered_set<std::string> &seen)
{
    auto push = [&](const PrintStateBase::StateWithWarnings &st, const char *code) {
        for (const auto &w : st.warnings)
            add_warning(report, w.level == PrintStateBase::WarningLevel::CRITICAL ? std::string(code) + "_critical"
                                                                                   : std::string(code),
                        w.message, &seen);
    };
    for (int s = 0; s < int(psCount); ++s)
        push(print.step_state_with_warnings(PrintStep(s)), "print");
    for (const PrintObject *po : print.objects())
        for (int s = 0; s < int(posCount); ++s)
            push(po->step_state_with_warnings(PrintObjectStep(s)), "object");
}

double ms_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------- placeholder vars
// Orca-profile compatibility: custom G-code from OrcaSlicer vendor profiles
// references options preFlight doesn't define (activate_air_filtration,
// flush_temperatures, ...) and Orca's computed variables. The host sends their
// (Orca-resolved) values with Orca schema types; each one preFlight neither
// defines nor aliases is registered on the print's placeholder parser, which
// G-code export copies. Values that fail to parse are skipped.
ConfigOption *make_placeholder_option(const std::string &type, const json &value)
{
    auto join = [&](const char *sep) {
        if (!value.is_array()) return cs::scalar_to_string(value);
        std::string out;
        for (size_t i = 0; i < value.size(); ++i) { if (i) out += sep; out += cs::scalar_to_string(value[i]); }
        return out;
    };
    auto first = [&]() { return value.is_array() ? (value.empty() ? std::string() : cs::scalar_to_string(value[0])) : cs::scalar_to_string(value); };
    std::unique_ptr<ConfigOption> opt;
    std::string text;
    if (type == "strings") {
        std::vector<std::string> v;
        if (value.is_array()) for (const json &e : value) v.push_back(cs::scalar_to_string(e));
        else v.push_back(cs::scalar_to_string(value));
        return new ConfigOptionStrings(v);
    }
    if (type == "string" || type == "enum") return new ConfigOptionString(first());
    if (type == "bool")               { opt.reset(new ConfigOptionBool());            text = first(); }
    else if (type == "int")           { opt.reset(new ConfigOptionInt());             text = first(); }
    else if (type == "float")         { opt.reset(new ConfigOptionFloat());           text = first(); }
    else if (type == "percent")       { opt.reset(new ConfigOptionPercent());         text = first(); }
    else if (type == "floatOrPercent"){ opt.reset(new ConfigOptionFloatOrPercent());  text = first(); }
    else if (type == "bools")         { opt.reset(new ConfigOptionBools());           text = join(","); }
    else if (type == "ints")          { opt.reset(new ConfigOptionInts());            text = join(","); }
    else if (type == "floats")        { opt.reset(new ConfigOptionFloats());          text = join(","); }
    else if (type == "percents")      { opt.reset(new ConfigOptionPercents());        text = join(","); }
    else if (type == "floatsOrPercents") { opt.reset(new ConfigOptionFloatsOrPercents()); text = join(","); }
    else if (type == "point")         { opt.reset(new ConfigOptionPoint());           text = first(); }
    else if (type == "points")        { opt.reset(new ConfigOptionPoints());          text = join(","); }
    else return nullptr;
    // Orca serialises bools as true/false in places; ConfigOptionBool wants 1/0.
    if (type == "bool" || type == "bools") {
        for (const char *w : {"true", "false"}) for (size_t p; (p = text.find(w)) != std::string::npos;) text.replace(p, std::strlen(w), w[0] == 't' ? "1" : "0");
    }
    try {
        if (!opt->deserialize(text)) return nullptr;
    } catch (...) { return nullptr; }
    return opt.release();
}

int apply_placeholder_vars(Print &print, const json &vars)
{
    if (!vars.is_object() || vars.empty()) return 0;
    auto &parser = const_cast<PlaceholderParser &>(print.placeholder_parser());
    const auto &aliases = orca_gcode_aliases();
    int n = 0;
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        const std::string &key = it.key();
        if (key.empty() || key.size() > 128 || !it->is_object()) continue;
        if (print_config_def.has(key) || parser.config().has(key)) continue;
        const json &spec = *it;
        const std::string type = spec.value("type", std::string());
        auto v = spec.find("value");
        if (v == spec.end()) continue;
        std::unique_ptr<ConfigOption> opt(make_placeholder_option(type, *v));
        if (!opt) continue;
        // An Orca alias wins unless it changes the shape: e.g. Orca's scalar
        // bed_temperature_initial_layer_single aliases preFlight's per-extruder
        // first_layer_bed_temperature, which `{if bed_temperature_initial_layer_single < 71}`
        // can't use. A direct variable is looked up before aliases.
        if (auto a = aliases.find(key); a != aliases.end())
            if (const ConfigOptionDef *def = print_config_def.get(a->second); def && def->is_scalar() == opt->is_scalar()) continue;
        parser.set(key, opt.release());
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------- slicing
int slice_impl(const char *job_json, int job_len, const uint8_t *blob, int blob_len, std::string &gcode, json &report)
{
    cs::Job job = cs::parse_job(job_json, job_len, blob, blob_len);

    ApplyLog log;
    DynamicPrintConfig config = build_print_config(job.config, log);
    if (config.opt_bool("binary_gcode")) {
        config.set_key_value("binary_gcode", new ConfigOptionBool(false));
        add_warning(report, "binary_gcode", "binary_gcode was requested; the WebAssembly engine emits text G-code");
    }

    cs::progress(0, "Preparing model");
    Model model;
    for (cs::MeshInput &m : job.objects) {
        ModelObject *mo = model.add_object();
        mo->name = m.name;
        ModelVolume *mv = mo->add_volume(make_mesh(m.positions, m.indices, m.transform, m.name));
        mv->name = m.name;
        // Extra volumes: parts, negative parts, modifiers, support blockers / enforcers.
        for (cs::VolumeInput &p : m.parts) {
            const ModelVolumeType type = p.type == "negative"         ? ModelVolumeType::NEGATIVE_VOLUME
                                       : p.type == "modifier"         ? ModelVolumeType::PARAMETER_MODIFIER
                                       : p.type == "support_blocker"  ? ModelVolumeType::SUPPORT_BLOCKER
                                       : p.type == "support_enforcer" ? ModelVolumeType::SUPPORT_ENFORCER
                                                                      : ModelVolumeType::MODEL_PART;
            ModelVolume *pv = mo->add_volume(make_mesh(p.positions, p.indices, p.transform, p.name), type);
            pv->name = p.name;
            if (!p.config.empty()) {
                ApplyLog vlog;
                apply_json_config(pv->config, p.config, vlog, p.name + ".");
                for (auto &sub : vlog.substitutions)
                    log.substitutions.push_back(sub);
                for (auto &k : vlog.unknown)
                    log.unknown.push_back(k);
            }
        }
        mo->add_instance();
        if (!m.config.empty()) {
            ApplyLog olog;
            apply_json_config(mo->config, m.config, olog, m.name + ".");
            for (auto &s : olog.substitutions)
                log.substitutions.push_back(s);
            for (auto &k : olog.unknown)
                log.unknown.push_back(k);
        }
        mo->invalidate_bounding_box();
        if (job.drop_to_bed)
            mo->ensure_on_bed();
    }
    report_apply_log(report, log);

    // Same as the preFlight CLI: instances not fully inside the build volume are not printed.
    {
        const Pointfs bed_shape = config.option<ConfigOptionPoints>("bed_shape")->values;
        BuildVolume build_volume(bed_shape, config.opt_float("max_print_height"));
        s_multiple_beds.update_build_volume(BoundingBoxf(bed_shape));
        model.update_print_volume_state(build_volume);
        for (const ModelObject *mo : model.objects)
            for (const ModelInstance *mi : mo->instances) {
                if (mi->print_volume_state == ModelInstancePVS_Fully_Outside)
                    add_warning(report, "outside_bed", mo->name + " is outside the print volume and was skipped");
                else if (mi->print_volume_state == ModelInstancePVS_Partly_Outside)
                    add_warning(report, "outside_bed", mo->name + " is partly outside the print volume and was skipped");
            }
    }

    Print print;
    int last_percent = 0;
    print.set_status_callback([&last_percent](const PrintBase::SlicingStatus &st) {
        if (st.percent < 0)
            return; // warning-update notification, collected after the run
        last_percent = std::max(last_percent, std::min(st.percent, 99));
        cs::progress(last_percent, st.text);
    });

    std::vector<std::string> apply_warnings;
    print.apply(model, config, &apply_warnings);
    std::unordered_set<std::string> seen;
    for (const std::string &w : apply_warnings)
        add_warning(report, "apply", w, &seen);
    apply_placeholder_vars(print, job.placeholder_vars);

    if (print.empty())
        throw Slic3r::SlicingError("Nothing to print: no object is inside the print volume or all objects are empty");

    if (job.validate) {
        std::vector<std::string> warnings;
        std::string err = print.validate(&warnings);
        for (const std::string &w : warnings)
            add_warning(report, "validate", w, &seen);
        if (!err.empty())
            throw Slic3r::SlicingError(err);
    }

    auto t0 = std::chrono::steady_clock::now();
    print.process();
    const double process_ms = ms_since(t0);

    auto t1 = std::chrono::steady_clock::now();
    GCodeProcessorResult result;
    print.export_gcode(std::string(), &result, nullptr);
    const double export_ms = ms_since(t1);

    if (result.gcode_object == nullptr)
        throw Slic3r::ExportError("G-code export produced no output buffer");
    gcode = result.gcode_object->text_buffer();
    if (gcode.empty())
        throw Slic3r::ExportError("G-code export produced an empty file");

    collect_step_warnings(print, report, seen);
    report["stats"] = collect_stats(print, result, config);
    report["timings"] = {{"processMs", process_ms}, {"exportMs", export_ms}};
    cs::progress(100, "Done");
    return 0;
}

// ---------------------------------------------------------------- schema
const char *type_name(ConfigOptionType t)
{
    switch (t) {
    case coFloat: return "float";
    case coFloats: return "floats";
    case coInt: return "int";
    case coInts: return "ints";
    case coString: return "string";
    case coStrings: return "strings";
    case coPercent: return "percent";
    case coPercents: return "percents";
    case coFloatOrPercent: return "floatOrPercent";
    case coFloatsOrPercents: return "floatsOrPercents";
    case coPoint: return "point";
    case coPoints: return "points";
    case coPoint3: return "point3";
    case coBool: return "bool";
    case coBools: return "bools";
    case coEnum: return "enum";
    case coEnums: return "enums";
    default: return "unknown";
    }
}

const char *gui_type_name(ConfigOptionDef::GUIType g)
{
    using G = ConfigOptionDef::GUIType;
    switch (g) {
    case G::i_enum_open: return "i_enum_open";
    case G::f_enum_open: return "f_enum_open";
    case G::select_open: return "select_open";
    case G::color: return "color";
    case G::slider: return "slider";
    case G::legend: return "legend";
    case G::one_string: return "one_string";
    case G::select_close: return "select_close";
    case G::password: return "password";
    default: return "";
    }
}

json bound(float v, bool is_min)
{
    if (is_min ? (v <= -FLT_MAX || v <= float(INT_MIN)) : (v >= FLT_MAX || v >= float(INT_MAX)))
        return nullptr;
    return double(v);
}

std::string build_schema()
{
    std::unordered_set<std::string> print_keys(Preset::print_options().begin(), Preset::print_options().end());
    std::unordered_set<std::string> filament_keys(Preset::filament_options().begin(), Preset::filament_options().end());
    std::unordered_set<std::string> printer_keys(Preset::printer_options().begin(), Preset::printer_options().end());
    const DynamicPrintConfig defaults = DynamicPrintConfig::full_print_config();

    std::set<std::string> keys;
    for (const std::string &k : defaults.keys())
        keys.insert(k);
    for (const auto *list : {&print_keys, &filament_keys, &printer_keys})
        for (const std::string &k : *list)
            if (print_config_def.has(k))
                keys.insert(k);

    json options = json::object();
    for (const std::string &key : keys) {
        const ConfigOptionDef *def = print_config_def.get(key);
        if (def == nullptr || def->type == coNone)
            continue;
        if (def->printer_technology == ptSLA)
            continue;
        json o;
        o["type"] = type_name(def->type);
        o["label"] = def->label;
        o["fullLabel"] = def->full_label.empty() ? def->label : def->full_label;
        o["category"] = def->category;
        o["tooltip"] = def->tooltip;
        o["sidetext"] = def->sidetext;
        o["mode"] = def->mode == comExpert ? "expert" : def->mode == comAdvanced ? "advanced" : "simple";
        o["min"] = bound(def->min, true);
        o["max"] = bound(def->max, false);
        const ConfigOption *dv = defaults.option(key);
        if (dv == nullptr)
            dv = def->default_value.get();
        o["default"] = dv ? dv->serialize() : std::string();
        if (def->enum_def && (def->enum_def->has_values() || def->enum_def->has_labels())) {
            o["enumValues"] = def->enum_def->enums();
            o["enumLabels"] = def->enum_def->labels();
        }
        o["multiline"] = def->multiline;
        o["fullWidth"] = def->full_width;
        o["readonly"] = def->readonly;
        o["nullable"] = def->nullable;
        o["guiType"] = gui_type_name(def->gui_type);
        // Keys shared by several preset types (inherits, compatible_printers[_condition], ...)
        // are attributed to the first of print, filament, machine.
        const char *scope = "other";
        if (print_keys.count(key))
            scope = "print";
        else if (filament_keys.count(key))
            scope = "filament";
        else if (printer_keys.count(key))
            scope = "machine";
        else if (PrintObjectConfig::defaults().has(key) || PrintRegionConfig::defaults().has(key))
            scope = "object";
        o["scope"] = scope;
        options[key] = std::move(o);
    }
    json out = {{"engine", "preflight"}, {"version", SLIC3R_VERSION}, {"options", std::move(options)}};
    return out.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string exception_message(const std::exception &e)
{
    std::string m = e.what();
    return m.empty() ? std::string("unknown error") : m;
}

} // namespace

// ================================================================ C ABI
extern "C" {

EMSCRIPTEN_KEEPALIVE const char *cs_version(void)
{
    static const std::string v = json{{"engine", "preflight"},
                                      {"version", SLIC3R_VERSION},
                                      {"bridge", cs::BRIDGE_ABI},
                                      {"build", CS_BUILD_VARIANT}}
                                     .dump();
    return v.c_str();
}

EMSCRIPTEN_KEEPALIVE int cs_describe_config(char **out_json, int *out_len)
{
    if (out_json)
        *out_json = nullptr;
    if (out_len)
        *out_len = 0;
    try {
        ensure_init();
        return cs::emit(build_schema(), out_json, out_len) == 0 ? 0 : 2;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "cs_describe_config: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "cs_describe_config: unknown exception\n");
        return 1;
    }
}

EMSCRIPTEN_KEEPALIVE int cs_slice(const char *job_json, int job_len, const uint8_t *blob, int blob_len,
                                  uint8_t **out_gcode, int *out_gcode_len, char **out_report, int *out_report_len)
{
    if (out_gcode)
        *out_gcode = nullptr;
    if (out_gcode_len)
        *out_gcode_len = 0;
    if (out_report)
        *out_report = nullptr;
    if (out_report_len)
        *out_report_len = 0;

    json report = cs::make_report();
    report["timings"] = {{"processMs", 0}, {"exportMs", 0}};
    int rc = 1;
    std::string gcode;
    try {
        ensure_init();
        rc = slice_impl(job_json, job_len, blob, blob_len, gcode, report);
    } catch (const cs::JobError &e) {
        rc = 2;
        report["error"] = std::string("invalid job: ") + e.what();
    } catch (const json::exception &e) {
        rc = 2;
        report["error"] = std::string("invalid job JSON: ") + e.what();
    } catch (const std::bad_alloc &) {
        rc = 4;
        report["error"] = "out of memory";
    } catch (const std::exception &e) {
        rc = 3;
        report["error"] = exception_message(e);
    } catch (...) {
        rc = 3;
        report["error"] = "unknown exception in slicing engine";
    }

    try {
        if (rc == 0 && out_gcode) {
            if (cs::emit(gcode, reinterpret_cast<char **>(out_gcode), out_gcode_len) != 0) {
                rc = 4;
                report["error"] = "out of memory while returning G-code";
            }
        }
        std::string().swap(gcode);
        report["ok"] = rc == 0;
        if (rc != 0)
            report["stats"] = nullptr;
        if (out_report) {
            std::string r = report.dump(-1, ' ', false, json::error_handler_t::replace);
            if (cs::emit(r, out_report, out_report_len) != 0 && rc == 0)
                rc = 4;
        }
    } catch (...) {
        if (rc == 0)
            rc = 4;
    }
    if (rc != 0 && out_gcode && *out_gcode) {
        std::free(*out_gcode);
        *out_gcode = nullptr;
        if (out_gcode_len)
            *out_gcode_len = 0;
    }
    return rc;
}

EMSCRIPTEN_KEEPALIVE int cs_eval_condition(const char *expr, int expr_len, const char *config_json, int config_len)
{
    try {
        ensure_init();
        if (expr_len < 0 || (expr_len > 0 && !expr))
            return -3;
        std::string e(expr ? expr : "", expr ? size_t(expr_len) : 0);
        if (std::all_of(e.begin(), e.end(), [](unsigned char c) { return std::isspace(c); }))
            return 1; // empty condition == compatible, as in PresetBundle
        json cfg_json;
        try {
            cfg_json = cs::parse_config_json(config_json, config_len);
        } catch (...) {
            return -2;
        }
        ApplyLog log;
        DynamicPrintConfig cfg = build_print_config(cfg_json, log);
        return PlaceholderParser::evaluate_boolean_expression(e, cfg) ? 1 : 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "cs_eval_condition: %s\n", e.what());
        return -1;
    } catch (...) {
        return -1;
    }
}

EMSCRIPTEN_KEEPALIVE void cs_free(void *p)
{
    std::free(p);
}

} // extern "C"
