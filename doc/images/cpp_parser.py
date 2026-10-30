import clang.cindex
import os
from clang.cindex import CursorKind, AccessSpecifier
import sys
# Set the path to your Clang library (if not in a standard location)
# clang.cindex.Config.set_library_path('/path/to/libclang.so')

# 1. Parse C++ code
index = clang.cindex.Index.create()
header_file = sys.argv[1]
tu = index.parse(header_file, args=['-x', 'c++-header', '-I/usr/include/c++/12', '-Iinclude/', '-I/usr/lib/clang/14.0.0/include/'])

gen_class_names_to_ignore = []
class_names_to_ignore = ['allocator','ios_base','exception','bad_exception','type_info','__class_type_info','type_info','bad_cast','bad_typeid','bad_alloc','bad_array_new_length','__make_unsigned_selector_base','type_info','exception_ptr','exception_ptr','nested_exception','locale','ctype','ctype','codecvt_base','codecvt','codecvt','codecvt','codecvt','time_base','money_base','__pthread_cleanup_class','allocator','allocator','allocator','__forced_unwind','__undefined','locale','facet','id','_Impl','facet','__shim','id','_Impl','collate','collate_byname','collate','collate_byname','logic_error','domain_error','invalid_argument','length_error','out_of_range','runtime_error','range_error','overflow_error','underflow_error','error_code','error_condition','system_error','error_category','error_code','error_condition','system_error','ios_base','failure','Init','ctype','ctype','ctype_byname','ctype_byname','__num_base','numpunct','numpunct_byname','ctype_byname','numpunct','numpunct_byname','ctype_byname','sentry','sentry','sentry','sentry']
std_cpp_member_types = ['char', 'float', 'double', 'std::string', 'std::wstring', ]

#print(std_cpp_member_types)
#mermaid_syntax = ["classDiagram"]
mermaid_syntax = []
inheritance_syntax = []
class_relationships_syntax = []

# 2. Traverse AST and extract class info
for cursor in tu.cursor.walk_preorder():
    class_name = ""
    parsing_class = False
    
    #Ignore everything expect the header file we are parsing
    if str(cursor.location.file) != header_file:
        continue

    if cursor.kind == clang.cindex.CursorKind.CLASS_DECL:
        parsing_class = True
        class_name = cursor.spelling
        #print(f"'{class_name}', ")
        #gen_class_names_to_ignore.append(f"'{class_name}'")
        #if class_name in class_names_to_ignore:
        #    continue
        mermaid_syntax.append(f"\tclass {class_name} {{")
    else:
        continue
    for child in cursor.get_children():
        if child.kind == CursorKind.CXX_METHOD:

            if child.access_specifier == AccessSpecifier.PUBLIC:
                mermaid_syntax.append(f"\t\t+ {child.spelling}()")
            elif child.access_specifier == AccessSpecifier.PRIVATE:
                mermaid_syntax.append(f"\t\t- {child.spelling}()")
            elif child.access_specifier == AccessSpecifier.PROTECTED:
                mermaid_syntax.append(f"\t\t# {child.spelling}()")

        elif child.kind == CursorKind.FIELD_DECL:
            if child.access_specifier == AccessSpecifier.PUBLIC:
                mermaid_syntax.append(f"\t\t+ {child.spelling}")
            elif child.access_specifier == AccessSpecifier.PRIVATE:
                mermaid_syntax.append(f"\t\t- {child.spelling}")
            elif child.access_specifier == AccessSpecifier.PROTECTED:
                mermaid_syntax.append(f"\t\t# {child.spelling}")

        elif child.kind == CursorKind.CXX_BASE_SPECIFIER:
            inheritance_syntax.append(f"\t {child.spelling} <|-- {cursor.spelling}")

        #print(len(child.spelling))
        #print(f"Checking if '{child.type.spelling} {child.spelling}' is in {std_cpp_member_types}")
        
        # Finding class relationships not working correctly
        #if (len(child.spelling) > 0) and (child.type.spelling not in std_cpp_member_types):
        #    if child.spelling[0] == "_":
                #print(f"{child.type.spelling} is not in std_cpp_member_types\n")
        #        member_type = f"{child.spelling[1].upper()}{child.spelling[2:]}"
        #        class_relationships_syntax.append(f"\t{member_type} -- {cursor.spelling}")
        
    if parsing_class:
        mermaid_syntax.append(f"\t}}")
    
#mermaid_syntax.append("\t}")
# Add logic for relationships (inheritance etc.)

# 3. Join the lines to form the complete Mermaid string
final_mermaid_code = "\n".join(mermaid_syntax)

# 4. (Optional) Render using Mermaid CLI or display in a tool
# Example for Mermaid CLI (requires external execution)
# import subprocess
# subprocess.run(['mmdc', '-i', '-', '-o', 'class_diagram.svg'], input=final_mermaid_code.encode())

#print(",".join(gen_class_names_to_ignore))


final_inheritance_code = "\n".join(inheritance_syntax)

#print(final_inheritance_code)
#print(final_mermaid_code)

#final_class_relationships_code = "\n".join(class_relationships_syntax)

#print(final_class_relationships_code)
print(final_inheritance_code)
print(final_mermaid_code)
#print(final_mermaid_code)
