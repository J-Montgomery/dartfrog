#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "datalog.hpp"
#include "dartfrog.hpp"
#include "utilities/interner.hh"
#include "benchmark/benchmark.h"

using namespace df::datalog;
using namespace util;
namespace fs = std::filesystem;

template <size_t N>
using Rows = std::vector<std::array<int32_t, N>>;

#define VEC_ARRAY(name, N) std::vector<std::array<int32_t, N>> name

template <size_t N>
Rows<N> read_facts(StringInterner &interner, const fs::path &dir, const char *name) {
    const fs::path path = dir / name;
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "could not open %s\n", path.c_str());
        std::abort();
    }
    Rows<N> rows;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::array<int32_t, N> row;
        size_t start = 0;
        for (size_t col = 0; col < N; ++col) {
            const size_t tab = line.find('\t', start);
            const std::string_view field(line.data() + start,
                                         (tab == std::string::npos ? line.size() : tab) - start);
            row[col] = interner.intern(field);
            start = (tab == std::string::npos) ? line.size() : tab + 1;
        }
        rows.push_back(row);
    }
    return rows;
}


template <size_t N>
Rows<N> dedup(Rows<N> rows) {
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

size_t countLines(const std::string& filename) {
    std::ifstream file(filename);
    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
    }
    return count;
}


template <size_t OutN, size_t InN>
Rows<OutN> project(const Rows<InN> &in, const std::array<size_t, OutN> &cols) {
    Rows<OutN> out;
    out.reserve(in.size());
    for (const std::array<int32_t, InN> &r : in) {
        std::array<int32_t, OutN> o;
        for (size_t i = 0; i < OutN; ++i) o[i] = r[cols[i]];
        out.push_back(o);
    }
    return out;
}

template <size_t N>
void insert(Predicate<int32_t, N> &pred, Rows<N> rows) {
    pred.insert(df::Relation<std::array<int32_t, N>>::from_vec(std::move(rows)));
}

struct DoopData {
    VEC_ARRAY(direct_superclass_in, 2);
    VEC_ARRAY(direct_superinterface_in, 2);
    VEC_ARRAY(component_type_in, 2);
    VEC_ARRAY(class_type_in, 1);
    VEC_ARRAY(array_type_in, 1);
    VEC_ARRAY(interface_type_in, 1);
    VEC_ARRAY(application_type_in, 1);
    VEC_ARRAY(main_class_in, 1);
    VEC_ARRAY(normal_heap_in, 2);
    VEC_ARRAY(method_in, 7);
    VEC_ARRAY(method_modifier_in, 2);

    VEC_ARRAY(method_simplename, 2);
    VEC_ARRAY(method_declaringtype, 2);
    VEC_ARRAY(method_descriptor, 2);

    VEC_ARRAY(abstract_method, 1);
    VEC_ARRAY(public_method, 1);
    VEC_ARRAY(static_method, 1);

    VEC_ARRAY(formal_param, 3);
    VEC_ARRAY(actual_param, 3);
    VEC_ARRAY(assign_return_value, 2);
    VEC_ARRAY(return_in, 4);
    VEC_ARRAY(var_type, 2);
    VEC_ARRAY(this_var, 2);
    VEC_ARRAY(assign_heap_in, 6);
    VEC_ARRAY(assign_local_in, 5);
    VEC_ARRAY(assign_cast_in, 6);
    VEC_ARRAY(store_array_in, 5);
    VEC_ARRAY(load_array_in, 5);
    VEC_ARRAY(load_ifield_in, 6);
    VEC_ARRAY(store_ifield_in, 6);
    VEC_ARRAY(load_sfield_in, 5);
    VEC_ARRAY(store_sfield_in, 5);
    VEC_ARRAY(static_invoke_in, 4);
    VEC_ARRAY(special_invoke_in, 5);
    VEC_ARRAY(virtual_invoke_in, 5);
    VEC_ARRAY(field_in, 4);
    VEC_ARRAY(string_constant_in, 1);

    VEC_ARRAY(return_var, 2);
    VEC_ARRAY(assign_heap, 3);
    VEC_ARRAY(assign_local, 3);
    VEC_ARRAY(assign_cast, 4);
    VEC_ARRAY(store_array, 3);
    VEC_ARRAY(load_array, 3);
    VEC_ARRAY(load_ifield, 4);
    VEC_ARRAY(store_ifield, 4);
    VEC_ARRAY(load_sfield, 3);
    VEC_ARRAY(store_sfield, 3);
    VEC_ARRAY(static_invoke, 3);
    VEC_ARRAY(field_declaringtype, 2);

    VEC_ARRAY(vmi_in_method, 2);
    VEC_ARRAY(vmi_base, 2);
    VEC_ARRAY(vmi_simplename, 2);
    VEC_ARRAY(vmi_descriptor, 2);

    VEC_ARRAY(smi_in_method, 2);
    VEC_ARRAY(smi_base, 2);
    VEC_ARRAY(smi_method, 2);

    VEC_ARRAY(heap_type, 2);
};

DoopData load_doop_data(const fs::path &data_dir, StringInterner &interner) {
    DoopData data;

    data.direct_superclass_in = dedup(read_facts<2>(interner, data_dir, "DirectSuperclass.facts"));
    data.direct_superinterface_in = dedup(read_facts<2>(interner, data_dir, "DirectSuperinterface.facts"));

    data.direct_superclass_in = dedup(read_facts<2>(interner, data_dir, "DirectSuperclass.facts"));
    data.direct_superinterface_in = dedup(read_facts<2>(interner, data_dir, "DirectSuperinterface.facts"));
    data.component_type_in = dedup(read_facts<2>(interner, data_dir, "ComponentType.facts"));
    data.class_type_in = dedup(read_facts<1>(interner, data_dir, "ClassType.facts"));
    data.array_type_in = dedup(read_facts<1>(interner, data_dir, "ArrayType.facts"));
    data.interface_type_in = dedup(read_facts<1>(interner, data_dir, "InterfaceType.facts"));
    data.application_type_in = dedup(read_facts<1>(interner, data_dir, "ApplicationClass.facts"));
    data.main_class_in = dedup(read_facts<1>(interner, data_dir, "MainClass.facts"));
    data.normal_heap_in = read_facts<2>(interner, data_dir, "NormalHeap.facts");
    const auto method_in = read_facts<7>(interner, data_dir, "Method.facts");
    data.method_modifier_in = read_facts<2>(interner, data_dir, "Method-Modifier.facts");

    // Process Method data
    Rows<2> method_simplename, method_declaringtype, method_descriptor;
    method_simplename.reserve(method_in.size());
    method_declaringtype.reserve(method_in.size());
    method_descriptor.reserve(method_in.size());
    for (const auto &m : method_in) {
        method_simplename.push_back({m[0], m[1]});
        method_declaringtype.push_back({m[0], m[3]});
        const std::string descriptor = std::string(interner.label(m[4])) + std::string(interner.label(m[2]));
        method_descriptor.push_back({m[0], interner.intern(descriptor)});
    }
    data.method_simplename = dedup(std::move(method_simplename));
    data.method_declaringtype = dedup(std::move(method_declaringtype));
    data.method_descriptor = dedup(std::move(method_descriptor));

    // Process Modifiers
    const int32_t abstract_id = interner.intern("abstract"), public_id = interner.intern("public"), static_id = interner.intern("static");
    for (const auto &mm : data.method_modifier_in) {
        if (mm[0] == abstract_id) data.abstract_method.push_back({mm[1]});
        if (mm[0] == public_id) data.public_method.push_back({mm[1]});
        if (mm[0] == static_id) data.static_method.push_back({mm[1]});
    }
    data.abstract_method = dedup(std::move(data.abstract_method));
    data.public_method = dedup(std::move(data.public_method));
    data.static_method = dedup(std::move(data.static_method));

    // Load and Project
    data.formal_param = dedup(read_facts<3>(interner, data_dir, "FormalParam.facts"));
    data.actual_param = dedup(read_facts<3>(interner, data_dir, "ActualParam.facts"));
    data.assign_return_value = dedup(read_facts<2>(interner, data_dir, "AssignReturnValue.facts"));
    const auto return_in = read_facts<4>(interner, data_dir, "Return.facts");
    data.var_type = dedup(read_facts<2>(interner, data_dir, "Var-Type.facts"));
    data.this_var = dedup(read_facts<2>(interner, data_dir, "ThisVar.facts"));
    const auto assign_heap_in = read_facts<6>(interner, data_dir, "AssignHeapAllocation.facts");
    const auto assign_local_in = read_facts<5>(interner, data_dir, "AssignLocal.facts");
    const auto assign_cast_in = read_facts<6>(interner, data_dir, "AssignCast.facts");
    const auto store_array_in = read_facts<5>(interner, data_dir, "StoreArrayIndex.facts");
    const auto load_array_in = read_facts<5>(interner, data_dir, "LoadArrayIndex.facts");
    const auto load_ifield_in = read_facts<6>(interner, data_dir, "LoadInstanceField.facts");
    const auto store_ifield_in = read_facts<6>(interner, data_dir, "StoreInstanceField.facts");
    const auto load_sfield_in = read_facts<5>(interner, data_dir, "LoadStaticField.facts");
    const auto store_sfield_in = read_facts<5>(interner, data_dir, "StoreStaticField.facts");
    const auto static_invoke_in = read_facts<4>(interner, data_dir, "StaticMethodInvocation.facts");
    const auto special_invoke_in = read_facts<5>(interner, data_dir, "SpecialMethodInvocation.facts");
    const auto virtual_invoke_in = read_facts<5>(interner, data_dir, "VirtualMethodInvocation.facts");
    const auto field_in = read_facts<4>(interner, data_dir, "Field.facts");
    const auto string_constant_in = read_facts<1>(interner, data_dir, "StringConstant.facts");

    data.return_var = dedup(project<2, 4>(return_in, {2, 3}));
    data.assign_heap = dedup(project<3, 6>(assign_heap_in, {2, 3, 4}));
    data.assign_local = dedup(project<3, 5>(assign_local_in, {2, 3, 4}));
    data.assign_cast = dedup(project<4, 6>(assign_cast_in, {4, 2, 3, 5}));
    data.store_array = dedup(project<3, 5>(store_array_in, {2, 3, 4}));
    data.load_array = dedup(project<3, 5>(load_array_in, {3, 2, 4}));
    data.load_ifield = dedup(project<4, 6>(load_ifield_in, {3, 4, 2, 5}));
    data.store_ifield = dedup(project<4, 6>(store_ifield_in, {2, 3, 4, 5}));
    data.load_sfield = dedup(project<3, 5>(load_sfield_in, {3, 2, 4}));
    data.store_sfield = dedup(project<3, 5>(store_sfield_in, {2, 3, 4}));
    data.static_invoke = dedup(project<3, 4>(static_invoke_in, {0, 2, 3}));
    data.field_declaringtype = dedup(project<2, 4>(field_in, {0, 1}));

    // Method Invocation processing
    std::unordered_map<int32_t, int32_t> simplename_of, descriptor_of;
    for (const auto &e : data.method_simplename) simplename_of.emplace(e[0], e[1]);
    for (const auto &e : data.method_descriptor) descriptor_of.emplace(e[0], e[1]);

    for (const auto &v : virtual_invoke_in) {
        data.vmi_in_method.push_back({v[0], v[4]});
        data.vmi_base.push_back({v[0], v[3]});
        if (auto it = simplename_of.find(v[2]); it != simplename_of.end()) data.vmi_simplename.push_back({v[0], it->second});
        if (auto it = descriptor_of.find(v[2]); it != descriptor_of.end()) data.vmi_descriptor.push_back({v[0], it->second});
    }
    for (const auto &s : special_invoke_in) {
        data.smi_in_method.push_back({s[0], s[4]});
        data.smi_base.push_back({s[0], s[3]});
        data.smi_method.push_back({s[0], s[2]});
    }

    // Heap types
    data.heap_type = project<2, 2>(data.normal_heap_in, {0, 1});
    for (const auto &sc : string_constant_in) data.heap_type.push_back({sc[0], interner.intern("java.lang.String")});
    data.heap_type.push_back({interner.intern("<<main method array>>"), interner.intern("java.lang.String[]")});
    data.heap_type.push_back({interner.intern("<<main method array content>>"), interner.intern("java.lang.String")});
    data.heap_type = dedup(std::move(data.heap_type));

    return data;
}

std::array<size_t, 3> run_dartfrog_doop(const DoopData &data, StringInterner &interner) {

    Datalog dl;

    Predicate<int32_t, 1> class_type(dl);
    Predicate<int32_t, 1> array_type(dl);
    Predicate<int32_t, 1> interface_type(dl);
    Predicate<int32_t, 1> application_type(dl);
    Predicate<int32_t, 1> normal_heap_type(dl);
    Predicate<int32_t, 2> direct_superclass(dl);
    Predicate<int32_t, 2> direct_superinterface(dl);
    Predicate<int32_t, 2> component_type(dl);
    Predicate<int32_t, 2> method_simplename_p(dl);
    Predicate<int32_t, 2> method_descriptor_p(dl);
    Predicate<int32_t, 2> method_declaringtype_p(dl);
    Predicate<int32_t, 1> abstract_method_p(dl);
    Predicate<int32_t, 1> public_method_p(dl);
    Predicate<int32_t, 1> static_method_p(dl);
    Predicate<int32_t, 1> main_class(dl);

    const int32_t object_id = interner.intern("java.lang.Object");
    const int32_t cloneable_id = interner.intern("java.lang.Cloneable");
    const int32_t serializable_id = interner.intern("java.io.Serializable");
    Predicate<int32_t, 1> object_type(dl);
    Predicate<int32_t, 1> cloneable_type(dl);
    Predicate<int32_t, 1> serializable_type(dl);

    Predicate<int32_t, 1> is_type(dl);
    Predicate<int32_t, 1> is_reference_type(dl);
    Predicate<int32_t, 1> is_class_type(dl);
    Predicate<int32_t, 1> is_interface_type(dl);
    Predicate<int32_t, 1> is_array_type(dl);
    Predicate<int32_t, 4> method_implemented(dl);
    Predicate<int32_t, 3> method_implemented_proj(dl);
    Predicate<int32_t, 4> method_lookup(dl);
    Predicate<int32_t, 2> direct_subclass(dl);
    Predicate<int32_t, 2> subclass(dl);
    Predicate<int32_t, 2> superclass(dl);
    Predicate<int32_t, 2> superinterface(dl);
    Predicate<int32_t, 2> subtype_of(dl);
    Predicate<int32_t, 2> supertype_of(dl);
    Predicate<int32_t, 2> subtype_of_different(dl);
    Predicate<int32_t, 1> main_method_declaration(dl);

    {
        DL_VARS(c);
        dl.add_rule((is_type(c), is_reference_type(c), is_class_type(c)) %= class_type(c));
    }
    {
        DL_VARS(a);
        dl.add_rule((is_type(a), is_reference_type(a), is_array_type(a)) %= array_type(a));
    }
    {
        DL_VARS(i);
        dl.add_rule((is_type(i), is_reference_type(i), is_interface_type(i)) %= interface_type(i));
    }
    {
        DL_VARS(t);
        dl.add_rule((is_type(t), is_reference_type(t)) %= application_type(t));
    }
    {
        DL_VARS(t);
        dl.add_rule(is_type(t) %= normal_heap_type(t));
    }

    // MethodImplemented(s,d,type,m) :- Method_SimpleName(m,s), Method_Descriptor(m,d),
    //                                  Method_DeclaringType(m,type), ! Method_Modifier("abstract",
    //                                  m).
    {
        DL_VARS(s, d, type, m);
        dl.add_rule(method_implemented(s, d, type, m) %=
                    method_simplename_p(m, s) && method_descriptor_p(m, d) &&
                    method_declaringtype_p(m, type) && !abstract_method_p(m));
    }
    // MethodImplementedProj(s,d,type) :- MethodImplemented(s,d,type,_).  (wildcard m)
    {
        DL_VARS(s, d, type, m);
        dl.add_rule(method_implemented_proj(s, d, type) %= method_implemented(s, d, type, m));
    }
    // MethodLookup(s,d,type,m) :- MethodImplemented(s,d,type,m).
    {
        DL_VARS(s, d, type, m);
        dl.add_rule(method_lookup(s, d, type, m) %= method_implemented(s, d, type, m));
    }
    // MethodLookup(...) :- (DirectSuperclass(type,super) ; DirectSuperinterface(type,super)),
    //                      MethodLookup(s,d,super,m), ! MethodImplementedProj(s,d,type).
    {
        DL_VARS(s, d, type, m, super);
        dl.add_rule(method_lookup(s, d, type, m) %= direct_superclass(type, super) &&
                                                    method_lookup(s, d, super, m) &&
                                                    !method_implemented_proj(s, d, type));
    }
    {
        DL_VARS(s, d, type, m, super);
        dl.add_rule(method_lookup(s, d, type, m) %= direct_superinterface(type, super) &&
                                                    method_lookup(s, d, super, m) &&
                                                    !method_implemented_proj(s, d, type));
    }
    {
        DL_VARS(a, c);
        dl.add_rule(direct_subclass(a, c) %= direct_superclass(a, c));
    }
    {
        DL_VARS(a, c);
        dl.add_rule(subclass(c, a) %= direct_subclass(a, c));
    }
    {
        DL_VARS(a, b, c);
        dl.add_rule(subclass(c, a) %= subclass(b, a) && direct_subclass(b, c));
    }
    {
        DL_VARS(a, c);
        dl.add_rule(superclass(c, a) %= subclass(a, c));
    }
    {
        DL_VARS(c, k);
        dl.add_rule(superinterface(k, c) %= direct_superinterface(c, k));
    }
    {
        DL_VARS(c, j, k);
        dl.add_rule(superinterface(k, c) %= direct_superinterface(c, j) && superinterface(k, j));
    }
    {
        DL_VARS(c, k, super);
        dl.add_rule(superinterface(k, c) %=
                    direct_superclass(c, super) && superinterface(k, super));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(supertype_of(s, t) %= subtype_of(t, s));
    }
    {
        DL_VARS(s);
        dl.add_rule(subtype_of(s, s) %= is_class_type(s));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of(s, t) %= subclass(t, s));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of(s, t) %= is_class_type(s) && superinterface(t, s));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of(s, t) %= is_interface_type(s) && is_type(t) && object_type(t));
    }
    {
        DL_VARS(s);
        dl.add_rule(subtype_of(s, s) %= is_interface_type(s));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of(s, t) %= is_interface_type(s) && superinterface(t, s));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of(s, t) %= is_array_type(s) && is_type(t) && object_type(t));
    }
    {
        DL_VARS(s, t, sc, tc);
        dl.add_rule(subtype_of(s, t) %= component_type(s, sc) && component_type(t, tc) &&
                                        is_reference_type(sc) && is_reference_type(tc) &&
                                        subtype_of(sc, tc));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of(s, t) %=
                    is_array_type(s) && is_interface_type(t) && is_type(t) && cloneable_type(t));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of(s, t) %=
                    is_array_type(s) && is_interface_type(t) && is_type(t) && serializable_type(t));
    }
    {
        DL_VARS(t);
        dl.add_rule(subtype_of(t, t) %= is_type(t));
    }
    {
        DL_VARS(s, t);
        dl.add_rule(subtype_of_different(s, t) %=
                    subtype_of(s, t) &&
                    df::datalog::where<0, 1>([](int32_t a, int32_t b) { return a != b; }));
    }

    // MainMethodDeclaration.
    const int32_t main_name_id = interner.intern("main");
    const int32_t main_desc_id = interner.intern("void(java.lang.String[])");
    const int32_t excl1 =
        interner.intern("<java.util.prefs.Base64: void main(java.lang.String[])>");
    const int32_t excl2 =
        interner.intern("<sun.java2d.loops.GraphicsPrimitiveMgr: void main(java.lang.String[])>");
    const int32_t excl3 =
        interner.intern("<sun.security.provider.PolicyParser: void main(java.lang.String[])>");
    {
        DL_VARS(method, type, sn, desc);
        dl.add_rule(
            main_method_declaration(method) %=
            main_class(type) && method_declaringtype_p(method, type) &&
            method_simplename_p(method, sn) && method_descriptor_p(method, desc) &&
            public_method_p(method) && static_method_p(method) &&
            df::datalog::where<0>([excl1, excl2, excl3](int32_t m) {
                return m != excl1 && m != excl2 && m != excl3;
            }) &&
            df::datalog::where<2>([main_name_id](int32_t s) { return s == main_name_id; }) &&
            df::datalog::where<3>([main_desc_id](int32_t d) { return d == main_desc_id; }));
    }

    Predicate<int32_t, 3> formal_param_p(dl);        
    Predicate<int32_t, 3> actual_param_p(dl);        
    Predicate<int32_t, 2> assign_return_value_p(dl); 
    Predicate<int32_t, 2> return_var_p(dl);          
    Predicate<int32_t, 2> var_type_p(dl);            
    Predicate<int32_t, 2> this_var_p(dl);            
    Predicate<int32_t, 3> assign_heap_p(dl);         
    Predicate<int32_t, 3> assign_local_p(dl);        
    Predicate<int32_t, 4> assign_cast_p(dl);         
    Predicate<int32_t, 3> store_array_p(dl);         
    Predicate<int32_t, 3> load_array_p(dl);          
    Predicate<int32_t, 4> load_ifield_p(dl);         
    Predicate<int32_t, 4> store_ifield_p(dl);        
    Predicate<int32_t, 3> load_sfield_p(dl);         
    Predicate<int32_t, 3> store_sfield_p(dl);        
    Predicate<int32_t, 3> static_invoke_p(dl);       
    Predicate<int32_t, 2> field_declaringtype_p(dl); 
    Predicate<int32_t, 2> heap_type_p(dl);           
    Predicate<int32_t, 2> vmi_in_method_p(dl);       
    Predicate<int32_t, 2> vmi_base_p(dl);            
    Predicate<int32_t, 2> vmi_simplename_p(dl);      
    Predicate<int32_t, 2> vmi_descriptor_p(dl);      
    Predicate<int32_t, 2> smi_in_method_p(dl);       
    Predicate<int32_t, 2> smi_base_p(dl);            
    Predicate<int32_t, 2> smi_method_p(dl);          

    Predicate<int32_t, 2> assign(dl);                  
    Predicate<int32_t, 2> var_points_to(dl);           
    Predicate<int32_t, 3> instance_field_points_to(dl);
    Predicate<int32_t, 2> static_field_points_to(dl);  
    Predicate<int32_t, 2> call_graph_edge(dl);         
    Predicate<int32_t, 2> array_index_points_to(dl);   
    Predicate<int32_t, 1> reachable(dl);               
    Predicate<int32_t, 2> class_initializer(dl);       
    Predicate<int32_t, 1> initialized_class(dl);       

    // Assign(?actual, ?formal) :- CallGraphEdge(?inv, ?method), FormalParam(?index, ?method, ?formal),
    //                             ActualParam(?index, ?inv, ?actual).
    { DL_VARS(actual, formal, inv, method, index);
      dl.add_rule(assign(actual, formal) %=
                  call_graph_edge(inv, method) && formal_param_p(index, method, formal) &&
                  actual_param_p(index, inv, actual)); }
    // Assign(?return, ?local) :- CallGraphEdge(?inv, ?method), ReturnVar(?return, ?method),
    //                            AssignReturnValue(?inv, ?local).
    { DL_VARS(ret, local, inv, method);
      dl.add_rule(assign(ret, local) %=
                  call_graph_edge(inv, method) && return_var_p(ret, method) &&
                  assign_return_value_p(inv, local)); }
    // VarPointsTo(?heap, ?var) :- AssignHeapAllocation(?heap, ?var, ?inMethod), Reachable(?inMethod).
    { DL_VARS(heap, var, in_method);
      dl.add_rule(var_points_to(heap, var) %=
                  assign_heap_p(heap, var, in_method) && reachable(in_method)); }
    // VarPointsTo(?heap, ?to) :- Assign(?from, ?to), VarPointsTo(?heap, ?from).
    { DL_VARS(heap, to, from);
      dl.add_rule(var_points_to(heap, to) %= assign(from, to) && var_points_to(heap, from)); }
    // VarPointsTo(?heap, ?to) :- Reachable(?m), AssignLocal(?from, ?to, ?m), VarPointsTo(?heap, ?from).
    { DL_VARS(heap, to, from, m);
      dl.add_rule(var_points_to(heap, to) %=
                  reachable(m) && assign_local_p(from, to, m) && var_points_to(heap, from)); }
    // VarPointsTo(?heap, ?to) :- Reachable(?m), AssignCast(?type, ?from, ?to, ?m),
    //   basic.SupertypeOf(?type, ?heaptype), HeapAllocation_Type(?heap, ?heaptype),
    //   VarPointsTo(?heap, ?from).
    { DL_VARS(heap, to, from, m, type, heaptype);
      dl.add_rule(var_points_to(heap, to) %=
                  reachable(m) && assign_cast_p(type, from, to, m) && supertype_of(type, heaptype) &&
                  heap_type_p(heap, heaptype) && var_points_to(heap, from)); }
    // ArrayIndexPointsTo(?baseheap, ?heap) :- Reachable(?m), StoreArrayIndex(?from, ?base, ?m),
    //   VarPointsTo(?baseheap, ?base), VarPointsTo(?heap, ?from), HeapAllocation_Type(?heap, ?ht),
    //   HeapAllocation_Type(?baseheap, ?bht), ComponentType(?bht, ?ct), basic.SupertypeOf(?ct, ?ht).
    { DL_VARS(baseheap, heap, m, from, base, ht, bht, ct);
      dl.add_rule(array_index_points_to(baseheap, heap) %=
                  reachable(m) && store_array_p(from, base, m) && var_points_to(baseheap, base) &&
                  var_points_to(heap, from) && heap_type_p(heap, ht) && heap_type_p(baseheap, bht) &&
                  component_type(bht, ct) && supertype_of(ct, ht)); }
    // VarPointsTo(?heap, ?to) :- Reachable(?m), LoadArrayIndex(?base, ?to, ?m),
    //   VarPointsTo(?baseheap, ?base), ArrayIndexPointsTo(?baseheap, ?heap), Var_Type(?to, ?type),
    //   HeapAllocation_Type(?baseheap, ?bht), ComponentType(?bht, ?bct), basic.SupertypeOf(?type, ?bct).
    { DL_VARS(heap, to, m, base, baseheap, type, bht, bct);
      dl.add_rule(var_points_to(heap, to) %=
                  reachable(m) && load_array_p(base, to, m) && var_points_to(baseheap, base) &&
                  array_index_points_to(baseheap, heap) && var_type_p(to, type) &&
                  heap_type_p(baseheap, bht) && component_type(bht, bct) && supertype_of(type, bct)); }
    // VarPointsTo(?heap, ?to) :- Reachable(?m), LoadInstanceField(?base, ?sig, ?to, ?m),
    //   VarPointsTo(?baseheap, ?base), InstanceFieldPointsTo(?heap, ?sig, ?baseheap).
    { DL_VARS(heap, to, m, base, sig, baseheap);
      dl.add_rule(var_points_to(heap, to) %=
                  reachable(m) && load_ifield_p(base, sig, to, m) && var_points_to(baseheap, base) &&
                  instance_field_points_to(heap, sig, baseheap)); }
    // InstanceFieldPointsTo(?heap, ?fld, ?baseheap) :- Reachable(?m),
    //   StoreInstanceField(?from, ?base, ?fld, ?m), VarPointsTo(?heap, ?from), VarPointsTo(?baseheap, ?base).
    { DL_VARS(heap, fld, baseheap, m, from, base);
      dl.add_rule(instance_field_points_to(heap, fld, baseheap) %=
                  reachable(m) && store_ifield_p(from, base, fld, m) && var_points_to(heap, from) &&
                  var_points_to(baseheap, base)); }
    // VarPointsTo(?heap, ?to) :- Reachable(?m), LoadStaticField(?fld, ?to, ?m),
    //   StaticFieldPointsTo(?heap, ?fld).
    { DL_VARS(heap, to, m, fld);
      dl.add_rule(var_points_to(heap, to) %=
                  reachable(m) && load_sfield_p(fld, to, m) && static_field_points_to(heap, fld)); }
    // StaticFieldPointsTo(?heap, ?fld) :- Reachable(?m), StoreStaticField(?from, ?fld, ?m),
    //   VarPointsTo(?heap, ?from).
    { DL_VARS(heap, fld, m, from);
      dl.add_rule(static_field_points_to(heap, fld) %=
                  reachable(m) && store_sfield_p(from, fld, m) && var_points_to(heap, from)); }
    // VarPointsTo(?heap, ?this) :- Reachable(?inM), Instruction_Method(?inv, ?inM),
    //   VirtualMethodInvocation_Base(?inv, ?base), VarPointsTo(?heap, ?base),
    //   HeapAllocation_Type(?heap, ?ht), VirtualMethodInvocation_SimpleName(?inv, ?sn),
    //   VirtualMethodInvocation_Descriptor(?inv, ?d), basic.MethodLookup(?sn, ?d, ?ht, ?toM),
    //   ThisVar(?toM, ?this).
    { DL_VARS(heap, self, in_m, inv, base, ht, sn, d, to_m);
      dl.add_rule(var_points_to(heap, self) %=
                  reachable(in_m) && vmi_in_method_p(inv, in_m) && vmi_base_p(inv, base) &&
                  var_points_to(heap, base) && heap_type_p(heap, ht) && vmi_simplename_p(inv, sn) &&
                  vmi_descriptor_p(inv, d) && method_lookup(sn, d, ht, to_m) &&
                  this_var_p(to_m, self)); }
    // Reachable(?toM), CallGraphEdge(?inv, ?toM) :- [same 8-way join without ThisVar].
    { DL_VARS(to_m, inv, in_m, base, heap, ht, sn, d);
      dl.add_rule((reachable(to_m), call_graph_edge(inv, to_m)) %=
                  reachable(in_m) && vmi_in_method_p(inv, in_m) && vmi_base_p(inv, base) &&
                  var_points_to(heap, base) && heap_type_p(heap, ht) && vmi_simplename_p(inv, sn) &&
                  vmi_descriptor_p(inv, d) && method_lookup(sn, d, ht, to_m)); }
    // Reachable(?toM), CallGraphEdge(?inv, ?toM) :- Reachable(?inM),
    //   StaticMethodInvocation(?inv, ?toM, ?inM).
    { DL_VARS(to_m, inv, in_m);
      dl.add_rule((reachable(to_m), call_graph_edge(inv, to_m)) %=
                  reachable(in_m) && static_invoke_p(inv, to_m, in_m)); }
    // Reachable(?toM), CallGraphEdge(?inv, ?toM), VarPointsTo(?heap, ?this) :- Reachable(?inM),
    //   Instruction_Method(?inv, ?inM), SpecialMethodInvocation_Base(?inv, ?base),
    //   VarPointsTo(?heap, ?base), MethodInvocation_Method(?inv, ?toM), ThisVar(?toM, ?this).
    { DL_VARS(to_m, inv, self, heap, in_m, base);
      dl.add_rule((reachable(to_m), call_graph_edge(inv, to_m), var_points_to(heap, self)) %=
                  reachable(in_m) && smi_in_method_p(inv, in_m) && smi_base_p(inv, base) &&
                  var_points_to(heap, base) && smi_method_p(inv, to_m) && this_var_p(to_m, self)); }
    // Reachable(?method) :- basic.MainMethodDeclaration(?method).
    { DL_VARS(method); dl.add_rule(reachable(method) %= main_method_declaration(method)); }

    const int32_t clinit_id = interner.intern("<clinit>");
    const int32_t void_paren_id = interner.intern("void()");
    // ClassInitializer(?type, ?method) :- basic.MethodImplemented("<clinit>", "void()", ?type, ?method).
    { DL_VARS(s, d, type, method);
      dl.add_rule(class_initializer(type, method) %=
                  method_implemented(s, d, type, method) &&
                  df::datalog::where<0>([clinit_id](int32_t v) { return v == clinit_id; }) &&
                  df::datalog::where<1>([void_paren_id](int32_t v) { return v == void_paren_id; })); }
    // InitializedClass(?super) :- InitializedClass(?class), DirectSuperclass(?class, ?super).
    { DL_VARS(super, klass);
      dl.add_rule(initialized_class(super) %=
                  initialized_class(klass) && direct_superclass(klass, super)); }
    // InitializedClass(?super) :- InitializedClass(?ci), DirectSuperinterface(?ci, ?super).
    { DL_VARS(super, ci);
      dl.add_rule(initialized_class(super) %=
                  initialized_class(ci) && direct_superinterface(ci, super)); }
    // InitializedClass(?class) :- basic.MainMethodDeclaration(?method), Method_DeclaringType(?method, ?class).
    { DL_VARS(klass, method);
      dl.add_rule(initialized_class(klass) %=
                  main_method_declaration(method) && method_declaringtype_p(method, klass)); }
    // InitializedClass(?class) :- Reachable(?inm), AssignHeapAllocation(?heap, _, ?inm),
    //   HeapAllocation_Type(?heap, ?class).
    { DL_VARS(klass, inm, heap, to);
      dl.add_rule(initialized_class(klass) %=
                  reachable(inm) && assign_heap_p(heap, to, inm) && heap_type_p(heap, klass)); }
    // InitializedClass(?class) :- Reachable(?inm), StaticMethodInvocation(?inv, ?sig, ?inm),
    //   Method_DeclaringType(?sig, ?class).
    { DL_VARS(klass, inm, inv, sig);
      dl.add_rule(initialized_class(klass) %=
                  reachable(inm) && static_invoke_p(inv, sig, inm) &&
                  method_declaringtype_p(sig, klass)); }
    // InitializedClass(?coi) :- Reachable(?inm), StoreStaticField(_, ?sig, ?inm),
    //   Field_DeclaringType(?sig, ?coi).
    { DL_VARS(coi, inm, from, sig);
      dl.add_rule(initialized_class(coi) %=
                  reachable(inm) && store_sfield_p(from, sig, inm) && field_declaringtype_p(sig, coi)); }
    // InitializedClass(?coi) :- Reachable(?inm), LoadStaticField(?sig, _, ?inm),
    //   Field_DeclaringType(?sig, ?coi).
    { DL_VARS(coi, inm, sig, to);
      dl.add_rule(initialized_class(coi) %=
                  reachable(inm) && load_sfield_p(sig, to, inm) && field_declaringtype_p(sig, coi)); }
    // Reachable(?clinit) :- InitializedClass(?class), ClassInitializer(?class, ?clinit).
    { DL_VARS(clinit, klass);
      dl.add_rule(reachable(clinit) %=
                  initialized_class(klass) && class_initializer(klass, clinit)); }

    insert(class_type, data.class_type_in);
    insert(array_type, data.array_type_in);
    insert(interface_type, data.interface_type_in);
    insert(application_type, data.application_type_in);
    insert(normal_heap_type, dedup(project<1, 2>(data.normal_heap_in, {1})));
    insert(direct_superclass, data.direct_superclass_in);
    insert(direct_superinterface, data.direct_superinterface_in);
    insert(component_type, data.component_type_in);
    insert(method_simplename_p, data.method_simplename);
    insert(method_descriptor_p, data.method_descriptor);
    insert(method_declaringtype_p, data.method_declaringtype);
    insert(abstract_method_p, data.abstract_method);
    insert(public_method_p, data.public_method);
    insert(static_method_p, data.static_method);
    insert(main_class, data.main_class_in);
    insert(object_type, Rows<1>{{object_id}});
    insert(cloneable_type, Rows<1>{{cloneable_id}});
    insert(serializable_type, Rows<1>{{serializable_id}});
    insert(formal_param_p, data.formal_param);
    insert(actual_param_p, data.actual_param);
    insert(assign_return_value_p, data.assign_return_value);
    insert(return_var_p, data.return_var);
    insert(var_type_p, data.var_type);
    insert(this_var_p, data.this_var);
    insert(assign_heap_p, data.assign_heap);
    insert(assign_local_p, data.assign_local);
    insert(assign_cast_p, data.assign_cast);
    insert(store_array_p, data.store_array);
    insert(load_array_p, data.load_array);
    insert(load_ifield_p, data.load_ifield);
    insert(store_ifield_p, data.store_ifield);
    insert(load_sfield_p, data.load_sfield);
    insert(store_sfield_p, data.store_sfield);
    insert(static_invoke_p, data.static_invoke);
    insert(field_declaringtype_p, data.field_declaringtype);
    insert(heap_type_p, data.heap_type);
    insert(vmi_in_method_p, data.vmi_in_method);
    insert(vmi_base_p, data.vmi_base);
    insert(vmi_simplename_p, data.vmi_simplename);
    insert(vmi_descriptor_p, data.vmi_descriptor);
    insert(smi_in_method_p, data.smi_in_method);
    insert(smi_base_p, data.smi_base);
    insert(smi_method_p, data.smi_method);

    dl.solve();

    const auto reachable_list = reachable.extract();
    const auto pointsto_list = var_points_to.extract();
    const auto callgraphedge_list = call_graph_edge.extract();

    benchmark::DoNotOptimize(reachable_list.data());
    benchmark::DoNotOptimize(pointsto_list.data());
    benchmark::DoNotOptimize(callgraphedge_list.data());
    benchmark::ClobberMemory();

    return {reachable_list.size(), pointsto_list.size(), callgraphedge_list.size()};
}

void BM_Dartfrog_Doop(benchmark::State &state) {
    fs::path data_dir(DATA_DIR);
    StringInterner interner;
    const DoopData data =
        load_doop_data(data_dir.string(), interner);

    std::array<size_t, 3> results;
    for (auto _ : state) {
        results = run_dartfrog_doop(data, interner);
    }
    state.counters["Reachable"] = static_cast<double>(results[0]);
    state.counters["VarPointsTo"] = static_cast<double>(results[1]);
    state.counters["CallGraphEdge"] = static_cast<double>(results[2]);
}

void BM_Souffle_Doop(benchmark::State &state) {
    const std::string bin_path = BIN_PATH;
    const std::string data_dir = DATA_DIR;
    const std::string souffle_args = SOUFFLE_ARGS;
    const fs::path output_dir(OUT_DIR);

    std::string cmd = bin_path + " -F " + data_dir + " -D " + output_dir.string() + " " + souffle_args;

    for (auto _ : state) {
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            state.SkipWithError("Souffle execution failed");
            break;
        }
    }
    state.counters["Reachable"] = static_cast<double>(countLines(output_dir / "Reachable.csv"));
    state.counters["VarPointsTo"] = static_cast<double>(countLines(output_dir / "VarPointsTo.csv"));
    state.counters["CallGraphEdge"] = static_cast<double>(countLines(output_dir / "CallGraphEdge.csv"));
}

BENCHMARK(BM_Dartfrog_Doop)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Souffle_Doop)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
