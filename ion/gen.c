char *gen_buf = NULL;

#define genf(...) buf_printf(gen_buf, __VA_ARGS__)
#define genlnf(...) (genln(), genf(__VA_ARGS__))

int gen_indent;
SrcPos gen_pos;

const char **gen_headers_buf;

char *gen_preamble_buf;
char *gen_postamble_buf;

void genln(void) {
    genf("\n%.*s", gen_indent * 4, "                                                                  ");
    gen_pos.line++;
}

bool is_incomplete_array_typespec(Typespec *typespec) {
    return typespec->kind == TYPESPEC_ARRAY && !typespec->num_elems;
}

char char_to_escape[256] = {
    ['\0'] = '0',
    ['\n'] = 'n',
    ['\r'] = 'r',
    ['\t'] = 't',
    ['\v'] = 'v',
    ['\b'] = 'b',
    ['\a'] = 'a',
    ['\\'] = '\\',
    ['"'] = '"',
    ['\''] = '\'',
};

const char *get_gen_name(const void *ptr);

void gen_char(char c) {
    if (char_to_escape[(unsigned char)c]) {
        genf("'\\%c'", char_to_escape[(unsigned char)c]);
    } else if (isprint(c)) {
        genf("'%c'", c);
    } else {
        genf("'\\x%X'", (unsigned char)c);
    }
}

void gen_str(const char *str, bool multiline) {
    if (multiline) {
        gen_indent++;
        genln();
    }
    genf("\"");
    while (*str) {
        const char *start = str;
        while (*str && isprint(*str) && !char_to_escape[(unsigned char)*str]) {
            str++;
        }
        if (start != str) {
            genf("%.*s", str - start, start);
        }
        if (*str) {
            if (char_to_escape[(unsigned char)*str]) {
                genf("\\%c", char_to_escape[(unsigned char)*str]);
                if (str[0] == '\n' && str[1]) {
                    genf("\"");
                    genlnf("\"");
                }
            } else {
                assert(!isprint(*str));
                genf("\\x%X", (unsigned char)*str);
            }
            str++;
        }
    }
    genf("\"");
    if (multiline) {
        gen_indent--;
    }
}

void gen_buf_pos(char **pbuf, SrcPos pos) {
    if (flag_nolinesync) {
        return;
    }
    char *buf = *pbuf;
    buf_printf(buf, "\n#line %d ", pos.line);
    char *old_gen_buf = gen_buf;
    gen_buf = buf;
    gen_str(pos.name, false);
    buf = gen_buf;
    gen_buf = old_gen_buf;
    buf_printf(buf, "\n");
    *pbuf = buf;
}

void gen_sync_pos(SrcPos pos) {
    if (flag_nolinesync) {
        return;
    }
    if (gen_pos.line != pos.line || gen_pos.name != pos.name) {
        genlnf("#line %d", pos.line);
        if (gen_pos.name != pos.name) {
            genf(" ");
            gen_str(pos.name, false);
        }
        gen_pos = pos;
    }
}

const char *cdecl_paren(const char *str, char c) {
    return c && c != '[' ? strf("(%s)", str) : str;
}

const char *cdecl_name(Type *type) {
    const char *type_name = type_names[type->kind];
    if (type_name) {
        return type_name;
    } else if (type->kind == TYPE_TUPLE) {
        return strf("tuple%d", type->typeid);
    } else {
        assert(type->sym);
        return get_gen_name(type->sym);
    }
}

char *type_to_cdecl(Type *type, const char *str) {
    switch (type->kind) {
    case TYPE_PTR:
        return type_to_cdecl(type->base, cdecl_paren(strf("*%s", str), *str));
    case TYPE_CONST:
        return type_to_cdecl(type->base, strf("const %s", cdecl_paren(str, *str)));
    case TYPE_ARRAY:
        if (type->num_elems == 0) {
            return type_to_cdecl(type->base, cdecl_paren(strf("%s[]", str), *str));
        } else {
            return type_to_cdecl(type->base, cdecl_paren(strf("%s[%zu]", str, type->num_elems), *str));
        }
    case TYPE_FUNC: {
        char *result = NULL;
        buf_printf(result, "(*%s)(", str);
        if (type->func.num_params == 0) {
            buf_printf(result, "void");
        } else {
            for (size_t i = 0; i < type->func.num_params; i++) {
                buf_printf(result, "%s%s", i == 0 ? "" : ", ", type_to_cdecl(type->func.params[i], ""));
            }
        }
        if (type->func.has_varargs) {
            buf_printf(result, ", ...");
        }
        buf_printf(result, ")");
        return type_to_cdecl(type->func.ret, result);
    }
    default:
        return strf("%s%s%s", cdecl_name(type), *str ? " " : "", str);
    }
}

void gen_expr(Expr *expr);

const char *gen_expr_str(Expr *expr) {
    char *temp = gen_buf;
    gen_buf = NULL;
    gen_expr(expr);
    const char *result = gen_buf;
    gen_buf = temp;
    return result;
}

Map gen_name_map;

const char *get_gen_name_or_default(const void *ptr, const char *default_name) {
    const char *name = map_get(&gen_name_map, ptr);
    if (!name) {
        Sym *sym = get_resolved_sym(ptr);
        if (sym) {
            if (sym->external_name) {
                name = sym->external_name;
            } else if (sym->home_package->external_name) {
                const char *external_name = sym->home_package->external_name;
                char buf[256];
                if (sym->kind == SYM_CONST) {
                    char *ptr = buf;
                    for (const char *str = external_name; *str && ptr < buf + sizeof(buf) - 1; str++, ptr++) {
                        *ptr = toupper(*str);
                    }
                    *ptr = 0;
                    if (ptr < buf + sizeof(buf)) {
                        external_name = buf;
                    }
                }
                name = strf("%s%s", external_name, sym->name);
            } else {
                name = sym->name;
            }
        } else {
            assert(default_name);
            name = default_name;
        }
        map_put(&gen_name_map, ptr, (void *)name);
    }
    return name;
}

const char *get_gen_name(const void *ptr) {
    const char *error = "ERROR";
    const char *name = get_gen_name_or_default(ptr, "error");
    assert(name != error);
    return name;
}

char *typespec_to_cdecl(Typespec *typespec, const char *str) {
    if (!typespec) {
        return strf("void%s%s", *str ? " " : "", str);
    }
    switch (typespec->kind) {
    case TYPESPEC_NAME:
        return strf("%s%s%s", get_gen_name(typespec), *str ? " " : "", str);
    case TYPESPEC_PTR:
        return typespec_to_cdecl(typespec->base, cdecl_paren(strf("*%s", str), *str));
    case TYPESPEC_CONST:
        return typespec_to_cdecl(typespec->base, str);
        // return typespec_to_cdecl(typespec->base, strf("const %s", cdecl_paren(str, *str)));
    case TYPESPEC_ARRAY:
        if (typespec->num_elems == 0) {
            return typespec_to_cdecl(typespec->base, cdecl_paren(strf("%s[]", str), *str));
        } else {
            return typespec_to_cdecl(typespec->base, cdecl_paren(strf("%s[%s]", str, gen_expr_str(typespec->num_elems)), *str));
        }
    case TYPESPEC_FUNC: {
        char *result = NULL;
        buf_printf(result, "(*%s)(", str);
        if (typespec->func.num_args == 0) {
            buf_printf(result, "void");
        } else {
            for (size_t i = 0; i < typespec->func.num_args; i++) {
                buf_printf(result, "%s%s", i == 0 ? "" : ", ", typespec_to_cdecl(typespec->func.args[i], ""));
            }
        }
        if (typespec->func.has_varargs) {
            buf_printf(result, ", ...");
        }
        buf_printf(result, ")");
        return typespec_to_cdecl(typespec->func.ret, result);
    }
    case TYPESPEC_TUPLE: {
        Type *type = get_resolved_type(typespec);
        return strf("tuple%d %s", type->typeid, str);
    }
    default:
        assert(0);
        return NULL;
    }
}

void gen_func_decl(Decl *decl) {
    assert(decl->kind == DECL_FUNC);
    char *result = NULL;
    buf_printf(result, "%s(", get_gen_name(decl));
    if (decl->func.num_params == 0) {
        buf_printf(result, "void");
    } else {
        for (size_t i = 0; i < decl->func.num_params; i++) {
            FuncParam param = decl->func.params[i];
            if (i != 0) {
                buf_printf(result, ", ");
            }
            buf_printf(result, "%s", type_to_cdecl(incomplete_decay(get_resolved_type(param.type)), param.name));
        }
    }
    if (decl->func.has_varargs) {
        buf_printf(result, ", ...");
    }
    buf_printf(result, ")");
    gen_sync_pos(decl->pos);
    if (decl->func.ret_type) {
        genlnf("%s", type_to_cdecl(incomplete_decay(get_resolved_type(decl->func.ret_type)), result));
    } else {
        genlnf("void %s", result);
    }
}

bool is_reachable(int reachable) {
    return flag_fullgen || reachable == REACHABLE_NATURAL;
}

bool is_sym_reachable(Sym *sym) {
    return is_reachable(sym->reachable);
}

bool is_tuple_reachable(Type *type) {
    return is_reachable(get_reachable(type));
}

void gen_forward_decls(void) {
    for (int i = 0; i < buf_len(tuple_types); i++) {
        Type *type = tuple_types[i];
        if (is_tuple_reachable(type)) {
            genlnf("typedef struct tuple%d tuple%d;", type->typeid, type->typeid);
        }
    }
    for (Sym **it = sorted_syms; it != buf_end(sorted_syms); it++) {
        Sym *sym = *it;
        Decl *decl = sym->decl;
        if (!decl || !is_sym_reachable(sym)) {
            continue;
        }
        if (is_decl_foreign(decl)) {
            continue;
        }
        switch (decl->kind) {
        case DECL_STRUCT:
        case DECL_UNION: {
            const char *name = get_gen_name(sym);
            genlnf("typedef %s %s %s;", decl->kind == DECL_STRUCT ? "struct" : "union", name, name);
            break;
        }
        default:
            // Do nothing.
            break;
        }
    }
}

void gen_aggregate_items(Aggregate *aggregate) {
    gen_indent++;
    for (size_t i = 0; i < aggregate->num_items; i++) {
        AggregateItem item = aggregate->items[i];
        if (item.kind == AGGREGATE_ITEM_FIELD) {
            for (size_t j = 0; j < item.num_names; j++) {
                gen_sync_pos(item.pos);
                if (item.type->kind == TYPESPEC_ARRAY && !item.type->num_elems) {
                    genlnf("%s;", typespec_to_cdecl(new_typespec_ptr(item.pos, item.type->base), item.names[j]));
                } else {
                    genlnf("%s;", typespec_to_cdecl(item.type, item.names[j]));
                }
            }
        } else if (item.kind == AGGREGATE_ITEM_SUBAGGREGATE) {
            genlnf("%s {", item.subaggregate->kind == AGGREGATE_STRUCT ? "struct" : "union");
            gen_aggregate_items(item.subaggregate);
            genlnf("};");
        } else {
            assert(0);
        }
    }
    gen_indent--;
}

void gen_aggregate(Decl *decl) {
    assert(decl->kind == DECL_STRUCT || decl->kind == DECL_UNION);
    if (decl->is_incomplete) {
        return;
    }
    genlnf("%s %s {", decl->kind == DECL_STRUCT ? "struct" : "union", get_gen_name(decl));
    gen_aggregate_items(decl->aggregate);
    genlnf("};");
}

void gen_expr(Expr *expr);

void gen_paren_expr(Expr *expr) {
    genf("(");
    gen_expr(expr);
    genf(")");
}

void gen_expr_compound(Expr *expr) {
    Type *expected_type = get_resolved_expected_type(expr);
    if (expected_type && !is_ptr_type(expected_type)) {
        genf("{");
    } else if (expr->compound.type) {
        genf("(%s){", typespec_to_cdecl(expr->compound.type, ""));
    } else {
        genf("(%s){", type_to_cdecl(get_resolved_type(expr), ""));
    }
    for (size_t i = 0; i < expr->compound.num_fields; i++) {
        if (i != 0) {
            genf(", ");
        }
        CompoundField field = expr->compound.fields[i];
        if (field.kind == FIELD_NAME) {
            genf(".%s = ", field.name);
        } else if (field.kind == FIELD_INDEX) {
            genf("[");
            gen_expr(field.index);
            genf("] = ");
        }
        gen_expr(field.init);
    }
    if (expr->compound.num_fields == 0) {
        genf("0");
    }
    genf("}");
}

const char *typeid_kind_names[NUM_TYPE_KINDS] = {
    [TYPE_NONE] = "TYPE_NONE",
    [TYPE_VOID] = "TYPE_VOID",
    [TYPE_BOOL] = "TYPE_BOOL",
    [TYPE_CHAR] = "TYPE_CHAR",
    [TYPE_UCHAR] = "TYPE_UCHAR",
    [TYPE_SCHAR] = "TYPE_SCHAR",
    [TYPE_SHORT] = "TYPE_SHORT",
    [TYPE_USHORT] = "TYPE_USHORT",
    [TYPE_INT] =  "TYPE_INT",
    [TYPE_UINT] = "TYPE_UINT",
    [TYPE_LONG] = "TYPE_LONG",
    [TYPE_ULONG] = "TYPE_ULONG",
    [TYPE_LLONG] = "TYPE_LLONG",
    [TYPE_ULLONG] = "TYPE_ULLONG",
    [TYPE_FLOAT] = "TYPE_FLOAT",
    [TYPE_DOUBLE] = "TYPE_DOUBLE",
    [TYPE_CONST] = "TYPE_CONST",
    [TYPE_PTR] = "TYPE_PTR",
    [TYPE_ARRAY] = "TYPE_ARRAY",
    [TYPE_STRUCT] = "TYPE_STRUCT",
    [TYPE_UNION] = "TYPE_UNION",
    [TYPE_FUNC] = "TYPE_FUNC",
    [TYPE_TUPLE] = "TYPE_TUPLE",
};

const char *typeid_kind_name(Type *type) {
    if (type->kind < NUM_TYPE_KINDS) {
        const char *name = typeid_kind_names[type->kind];
        if (name) {
            return name;
        }
    }
    return "TYPE_NONE";
}

bool is_excluded_typeinfo(Type *type) {
    while (type->kind == TYPE_ARRAY || type->kind == TYPE_CONST || type->kind == TYPE_PTR) {
        type = type->base;
    }
    if (type->sym) {
        if (get_decl_note(type->sym->decl, str_intern("notypeinfo"))) {
            return true;
        } else {
            return !is_sym_reachable(type->sym);
        }
    } else if (type->kind == TYPE_TUPLE) {
        return !is_tuple_reachable(type);
    } else {
        return !type->sym && (type->kind == TYPE_STRUCT || type->kind == TYPE_UNION);
    }
}

void gen_typeid(Type *type) {
    if (type->size == 0 || is_excluded_typeinfo(type)) {
        genf("TYPEID0(%d, %s)", type->typeid, typeid_kind_name(type));
    } else {
        genf("TYPEID(%d, %s, %s)", type->typeid, typeid_kind_name(type), type_to_cdecl(type, ""));
    }
}

void gen_intrinsic(Sym *sym, Expr *expr) {
    Type *type = get_resolved_type(expr->call.args[0]);
    Type *base = is_ptr_type(type) ? unqualify_type(type->base) : 0;
    Type *key = base && is_aggregate_type(base) && base->aggregate.num_fields == 2 ? base->aggregate.fields[0].type : 0;
    Type *val = base && is_aggregate_type(base) && base->aggregate.num_fields == 2 ? base->aggregate.fields[1].type : 0;
    if (sym->name == str_intern("va_copy") || sym->name == str_intern("va_start") || sym->name == str_intern("va_end")) {
        genf("%s(", sym->name);
        for (int i = 0; i < expr->call.num_args; i++) {
            if (i != 0) {
                genf(", ");
            }
            gen_expr(expr->call.args[i]);
        }
        genf(")");
    } else if (sym->name == str_intern("va_arg")) {
        assert(expr->call.num_args == 2);
        gen_expr(expr->call.args[1]);
        genf(" = va_arg(");
        gen_expr(expr->call.args[0]);
        Type *type = get_resolved_type(expr->call.args[1]);
        genf(", %s)", type_to_cdecl(type, ""));
    } else if (sym->name == str_intern("apush") || sym->name == str_intern("aputv") || sym->name == str_intern("adelv") ||
        sym->name == str_intern("agetvi") || sym->name == str_intern("agetvp") || sym->name == str_intern("agetv") ||
        sym->name == str_intern("asetcap") || sym->name == str_intern("afit") || sym->name == str_intern("acat") ||
        sym->name == str_intern("adeli") || sym->name == str_intern("aindexv") || sym->name == str_intern("asetlen")) {
        // (t, a, v)
        genf("%s(%s, (", sym->name, type_to_cdecl(base, ""));
        gen_expr(expr->call.args[0]);
        genf("), (");
        gen_expr(expr->call.args[1]);
        genf("))");
    } else if (sym->name == str_intern("adefault")) {
        // (t, tv, a, v)
        genf("%s(%s, %s, (", sym->name, type_to_cdecl(base, ""), type_to_cdecl(val, ""));
        gen_expr(expr->call.args[0]);
        genf("), (");
        gen_expr(expr->call.args[1]);
        genf("))");
    } else if (sym->name == str_intern("afill")) {
        // (t, a, v, n)
        genf("%s(%s, (", sym->name, type_to_cdecl(base, ""));
        gen_expr(expr->call.args[0]);
        genf("), (");
        gen_expr(expr->call.args[1]);
        genf("), (");
        gen_expr(expr->call.args[2]);
        genf("))");
    } else if (sym->name == str_intern("acatn") || sym->name == str_intern("adeln")) {
        genf("%s(%s, (", sym->name, type_to_cdecl(base, ""));
        gen_expr(expr->call.args[0]);
        genf("), (");
        gen_expr(expr->call.args[1]);
        genf("), (");
        gen_expr(expr->call.args[2]);
        genf("))");
    } else if (sym->name == str_intern("aindex") || sym->name == str_intern("ageti") || sym->name == str_intern("adel")) {
        genf("%s(%s, %s, (", sym->name, type_to_cdecl(base, ""), type_to_cdecl(key, ""));
        gen_expr(expr->call.args[0]);
        genf("), (");
        gen_expr(expr->call.args[1]);
        genf("))");
    } else if (sym->name == str_intern("agetp") || sym->name == str_intern("aget")) {
        genf("%s(%s, %s, %s, (", sym->name, type_to_cdecl(base, ""), type_to_cdecl(key, ""), type_to_cdecl(val, ""));
        gen_expr(expr->call.args[0]);
        genf("), (");
        gen_expr(expr->call.args[1]);
        genf("))");
    } else if (sym->name == str_intern("aput")) {
        genf("%s(%s, %s, (", sym->name, type_to_cdecl(base, ""), type_to_cdecl(key, ""));
        gen_expr(expr->call.args[0]);
        genf("), (");
        gen_expr(expr->call.args[1]);
        genf("), (");
        gen_expr(expr->call.args[2]);
        genf("))");
    } else if (sym->name == str_intern("ahdrsize") || sym->name == str_intern("ahdralign") || sym->name == str_intern("ahdr") ||
        sym->name == str_intern("alen") || sym->name == str_intern("acap") || sym->name == str_intern("afree") ||
        sym->name == str_intern("aclear") || sym->name == str_intern("apop")) {
        genf("%s(%s, (", sym->name, type_to_cdecl(base, ""));
        gen_expr(expr->call.args[0]);
        genf("))");
    } else if (sym->name == str_intern("anew")) {
        Type *result_type = get_resolved_type(expr);
        assert(is_ptr_type(result_type));
        genf("%s(%s, ", sym->name, type_to_cdecl(result_type->base, ""));
        gen_expr(expr->call.args[0]);
        genf(")");
    } else {
        fatal_error(expr->pos, "Call to unimplemented intrinsic %s", sym->name);
    }
}

void gen_expr_new(Expr *expr) {
    assert(expr->kind == EXPR_NEW);
    Type *type = get_resolved_type(expr);
    assert(is_ptr_type(type));
    const char *type_cdecl = type_to_cdecl(type, "");
    const char *base_cdecl = type_to_cdecl(type->base, "");
    if (expr->new_expr.alloc) {
        if (expr->new_expr.len) {
            if (!expr->new_expr.arg) {
                genf("((%s)generic_alloc((Allocator *)(", type_cdecl);
                gen_expr(expr->new_expr.alloc);
                genf("), ");
                gen_expr(expr->new_expr.len);
                genf("* sizeof(%s), alignof(%s)))", base_cdecl, base_cdecl);
            } else {
                genf("((%s)generic_alloc_copy((Allocator *)(", type_cdecl);
                gen_expr(expr->new_expr.alloc);
                genf("), ");
                gen_expr(expr->new_expr.len);
                genf(" * sizeof(%s), alignof(%s), &(", base_cdecl, base_cdecl);
                gen_expr(expr->new_expr.arg);
                genf(")))");
            }
        } else {
            if (!expr->new_expr.arg) {
                genf("((%s)generic_alloc((Allocator *)(", type_cdecl);
                gen_expr(expr->new_expr.alloc);
                genf("), ");
                genf("sizeof(%s), alignof(%s)))", base_cdecl, base_cdecl);
            } else {
                genf("((%s)generic_alloc_copy((Allocator *)(", type_cdecl);
                gen_expr(expr->new_expr.alloc);
                genf("), ");
                genf("sizeof(%s), alignof(%s), &(", base_cdecl, base_cdecl, base_cdecl);
                gen_expr(expr->new_expr.arg);
                genf(")))");
            }
        }
    } else {
        if (expr->new_expr.len) {
            if (!expr->new_expr.arg) {
                genf("((%s)tls_alloc(", type_cdecl);
                gen_expr(expr->new_expr.len);
                genf(" * sizeof(%s), alignof(%s)))", base_cdecl, base_cdecl);
            } else {
                genf("((%s)alloc_copy(", type_cdecl);
                gen_expr(expr->new_expr.len);
                genf(" * sizeof(%s), alignof(%s), &(", base_cdecl, base_cdecl);
                gen_expr(expr->new_expr.arg);
                genf(")))");
            }
        } else {
            if (!expr->new_expr.arg) {
                genf("((%s)tls_alloc(sizeof(%s), alignof(%s)))", type_cdecl, base_cdecl, base_cdecl);
            } else {
                genf("((%s)alloc_copy(sizeof(%s), alignof(%s), &(", type_cdecl, base_cdecl, base_cdecl);
                gen_expr(expr->new_expr.arg);
                genf(")))");
            }
        }
    }
}

void gen_expr(Expr *expr) {
    Type *type = NULL;
    Type *conv = type_conv(expr);
    if (conv) {
        genf("(%s)(", type_to_cdecl(conv, ""));
    }
    bool gen_any = is_implicit_any(expr);
    if (gen_any) {
        type = get_resolved_type(expr);
        genf("(any){(%s[]){", type_to_cdecl(type, ""));
    }
    switch (expr->kind) {
    case EXPR_PAREN:
        genf("(");
        gen_expr(expr->paren.expr);
        genf(")");
        break;
    case EXPR_INT: {
        const char *suffix_name = token_suffix_names[expr->int_lit.suffix];
        switch (expr->int_lit.mod) {
        case MOD_BIN:
        case MOD_HEX:
            genf("0x%llx%s", expr->int_lit.val, suffix_name);
            break;
        case MOD_OCT:
            genf("0%llo%s", expr->int_lit.val, suffix_name);
            break;
        case MOD_CHAR:
            gen_char((char)expr->int_lit.val);
            break;
        default:
            genf("%llu%s", expr->int_lit.val, suffix_name);
            break;
        }
        break;
    }
    case EXPR_FLOAT: {
        bool is_double = expr->float_lit.suffix == SUFFIX_D;
        size_t len = expr->float_lit.end - expr->float_lit.start;
        genf("%.*s%s", is_double ? len-1 : len, expr->float_lit.start, is_double ? "" : "f");
        break;
    }
    case EXPR_STR:
        gen_str(expr->str_lit.val, expr->str_lit.mod == MOD_MULTILINE);
        break;
    case EXPR_NAME:
        genf("%s", get_gen_name_or_default(expr, expr->name));
        break;
    case EXPR_CAST:
        genf("(%s)(", typespec_to_cdecl(expr->cast.type, ""));
        gen_expr(expr->cast.expr);
        genf(")");
        break;
    case EXPR_CALL: {
        Sym *sym = get_resolved_sym(expr->call.expr);
        if (is_intrinsic(sym)) {
            gen_intrinsic(sym, expr);
        } else {
            if (sym && sym->kind == SYM_TYPE) {
                genf("(%s)", get_gen_name(sym));
            } else {
                gen_expr(expr->call.expr);
            }
            genf("(");
            for (size_t i = 0; i < expr->call.num_args; i++) {
                if (i != 0) {
                    genf(", ");
                }
                gen_expr(expr->call.args[i]);
            }
            genf(")");
        }
        break;
    }
    case EXPR_INDEX: {
        Type *type = unqualify_type(get_resolved_type(expr->index.expr));
        if (is_aggregate_type(type)) {
            gen_expr(expr->index.expr);
            genf(".");
            long long i = get_resolved_val(expr->index.index).ll;
            genf("%s", type->aggregate.fields[i].name);
        } else {
            gen_expr(expr->index.expr);
            genf("[");
            gen_expr(expr->index.index);
            genf("]");
        }
        break;
    }
    case EXPR_FIELD: {
        Sym *sym = get_resolved_sym(expr);
        if (sym) {
            genf("(%s)", get_gen_name(sym));
        } else {
            gen_expr(expr->field.expr);
            Type *type = unqualify_type(get_resolved_type(expr->field.expr));
            const char *name = expr->field.name;
            genf("%s%s", type->kind == TYPE_PTR ? "->" : ".", name);
        }
        break;
    }
    case EXPR_COMPOUND:
        gen_expr_compound(expr);
        break;
    case EXPR_UNARY:
        genf("%s(", token_kind_name(expr->unary.op));
        gen_expr(expr->unary.expr);
        genf(")");
        break;
    case EXPR_BINARY: {
        genf("(");
        Type *left_promo = pointer_promo_type(expr->binary.left);
        if (left_promo) {
            genf("(%s)", type_to_cdecl(left_promo, ""));
        }
        gen_expr(expr->binary.left);
        genf(") %s (", token_kind_name(expr->binary.op));
        Type *right_promo = pointer_promo_type(expr->binary.right);
        if (right_promo) {
            genf("(%s)", type_to_cdecl(right_promo, ""));
        }
        gen_expr(expr->binary.right);
        genf(")");
        break;
    }
    case EXPR_TERNARY:
        genf("(");
        gen_expr(expr->ternary.cond);
        genf(" ? ");
        gen_expr(expr->ternary.then_expr);
        genf(" : ");
        gen_expr(expr->ternary.else_expr);
        genf(")");
        break;
    case EXPR_SIZEOF_EXPR:
        genf("sizeof(");
        gen_expr(expr->sizeof_expr);
        genf(")");
        break;
    case EXPR_SIZEOF_TYPE:
        genf("sizeof(%s)", typespec_to_cdecl(expr->sizeof_type, ""));
        break;
    case EXPR_ALIGNOF_EXPR:
        genf("alignof(%s)", type_to_cdecl(get_resolved_type(expr->alignof_expr), ""));
        break;
    case EXPR_ALIGNOF_TYPE:
        genf("alignof(%s)", typespec_to_cdecl(expr->alignof_type, ""));
        break;
    case EXPR_TYPEOF_EXPR: {
        Type *type = get_resolved_type(expr->typeof_expr);
        assert(type->typeid);
        gen_typeid(type);
        break;
    }
    case EXPR_TYPEOF_TYPE: {
        Type *type = get_resolved_type(expr->typeof_type);
        assert(type->typeid);
        gen_typeid(type);
        break;
    }
    case EXPR_OFFSETOF:
        genf("offsetof(%s, %s)", typespec_to_cdecl(expr->offsetof_field.type, ""), expr->offsetof_field.name);
        break;
    case EXPR_MODIFY:
        if (!expr->modify.post) {
            genf("%s", token_kind_name(expr->modify.op));
        }
        gen_paren_expr(expr->modify.expr);
        if (expr->modify.post) {
            genf("%s", token_kind_name(expr->modify.op));
        }
        break;
    case EXPR_NEW:
        gen_expr_new(expr);
        break;
    default:
        assert(0);
    }
    if (gen_any) {
        genf("}, ");
        gen_typeid(type);
        genf("}");
    }
    if (conv) {
        genf(")");
    }
}

void gen_stmt(Stmt *stmt);

void gen_stmt_block(StmtList block) {
    genf("{");
    gen_indent++;
    for (size_t i = 0; i < block.num_stmts; i++) {
        gen_stmt(block.stmts[i]);
    }
    gen_indent--;
    genlnf("}");
}

void gen_simple_stmt(Stmt *stmt) {
    switch (stmt->kind) {
    case STMT_EXPR:
        gen_expr(stmt->expr);
        break;
    case STMT_INIT:
        if (stmt->init.type) {
            Typespec *init_typespec = stmt->init.type;
            bool incomplete = is_incomplete_array_typespec(stmt->init.type);
            if (incomplete && !stmt->init.expr) {
                Type *init_type = get_resolved_type(stmt->init.type);
                genf("%s = 0", type_to_cdecl(type_decay(init_type), stmt->init.name));
            } else {
                if (incomplete && is_ptr_type(get_resolved_type(stmt->init.expr))) {
                    genf("%s", type_to_cdecl(get_resolved_type(stmt->init.expr), stmt->init.name));
                    if (stmt->init.expr) {
                        if (!stmt->init.is_undef) {
                            genf(" = ");
                            gen_expr(stmt->init.expr);
                        }
                    } else {
                        genf(" = {0}");
                    }
                } else {
                    if (incomplete) {
                        Expr *size = new_expr_int(init_typespec->pos, get_resolved_type(stmt->init.expr)->num_elems, 0, 0);
                        init_typespec = new_typespec_array(init_typespec->pos, init_typespec->base, size);
                    }
                    genf("%s", typespec_to_cdecl(stmt->init.type, stmt->init.name));
                    if (stmt->init.expr) {
                        if (!stmt->init.is_undef) {
                            genf(" = ");
                            gen_expr(stmt->init.expr);
                        }
                    } else if (!stmt->init.is_undef) {
                        genf(" = {0}");
                    }
                }
            }
        } else {
            genf("%s = ", type_to_cdecl(unqualify_type(get_resolved_type(stmt->init.expr)), stmt->init.name));
            gen_expr(stmt->init.expr);
        }
        break;
    case STMT_ASSIGN: {
        Type *promo_type = pointer_promo_type(stmt->assign.left);
        if (promo_type) {
            assert(stmt->assign.op == TOKEN_ADD_ASSIGN);
            Type *left_type = get_resolved_type(stmt->assign.left);
            if (stmt->assign.left->kind == EXPR_NAME) {
                const char *name = get_gen_name_or_default(stmt->assign.left, stmt->assign.left->name);
                genf("%s = (char *)(%s) + ", name, name);
                gen_expr(stmt->assign.right);
            } else {
                // TODO: this is an ugly codegen template that needs to avoid both illegal aliasing and multiple evaluation.
                // However, 99.9% of use cases will use a name on the left-hand side, and we handle that cleanly.
                genf("do { %s = (%s)&(", type_to_cdecl(type_ptr(left_type), "__pp"), type_to_cdecl(type_ptr(left_type), ""));
                gen_expr(stmt->assign.left);
                genf("); *__pp = (%s)(*(char **)__pp + ", type_to_cdecl(left_type, ""));
                gen_expr(stmt->assign.right);
                genf("); } while(0)");
            }
        } else {
            gen_expr(stmt->assign.left);
            genf(" %s ", token_kind_name(stmt->assign.op));
            gen_expr(stmt->assign.right);
        }
        break;
    }
    default:
        assert(0);
    }
}

static bool is_char_lit(Expr *expr) {
    return expr->kind == EXPR_INT && expr->int_lit.mod == MOD_CHAR;
}

void gen_stmt(Stmt *stmt) {
    gen_sync_pos(stmt->pos);
    switch (stmt->kind) {
    case STMT_RETURN:
        genlnf("return");
        if (stmt->expr) {
            genf(" ");
            gen_expr(stmt->expr);
        }
        genf(";");
        break;
    case STMT_BREAK:
        genlnf("break;");
        break;
    case STMT_CONTINUE:
        genlnf("continue;");
        break;
    case STMT_BLOCK:
        genln();
        gen_stmt_block(stmt->block);
        break;
    case STMT_NOTE: {
        Note note = stmt->note;
        if (note.name == assert_name) {
            genlnf("assert(");
            assert(note.num_args == 1);
            gen_expr(note.args[0].expr);
            genf(");");
        } else if (note.name == foreign_name) {
            const char *preamble_name = str_intern("preamble");
            const char *postamble_name = str_intern("postamble");
            for (size_t i = 0; i < note.num_args; i++) {
                const char *name = note.args[i].name;
                Expr *expr = note.args[i].expr;
                if (expr->kind != EXPR_STR) {
                    fatal_error(expr->pos, "#foreign argument must be a string");
                }
                const char *str = expr->str_lit.val;
                if (name == preamble_name) {
                    gen_buf_pos(&gen_preamble_buf, note.args[i].pos);
                    buf_printf(gen_preamble_buf, "%s\n", str);
                } else if (name == postamble_name) {
                    gen_buf_pos(&gen_postamble_buf, note.args[i].pos);
                    buf_printf(gen_postamble_buf, "%s\n", str);
                }
            }
        }
        break;
    }
    case STMT_IF:
        if (stmt->if_stmt.init) {
            genlnf("{");
            gen_indent++;
            gen_stmt(stmt->if_stmt.init);
        }
        gen_sync_pos(stmt->pos);
        genlnf("if (");
        if (stmt->if_stmt.cond) {
            gen_expr(stmt->if_stmt.cond);
        } else {
            genf("%s", stmt->if_stmt.init->init.name);
        }
        genf(") ");
        gen_stmt_block(stmt->if_stmt.then_block);
        for (size_t i = 0; i < stmt->if_stmt.num_elseifs; i++) {
            ElseIf elseif = stmt->if_stmt.elseifs[i];
            genf(" else if (");
            gen_expr(elseif.cond);
            genf(") ");
            gen_stmt_block(elseif.block);
        }
        if (stmt->if_stmt.else_block.stmts) {
            genf(" else ");
            gen_stmt_block(stmt->if_stmt.else_block);
        } else {
            Note *complete_note = get_stmt_note(stmt, complete_name);
            if (complete_note) {
                genf(" else {");
                gen_indent++;
                gen_sync_pos(complete_note->pos);
                genlnf("assert(\"@complete if/elseif chain failed to handle case\" && 0);");
                gen_indent--;
                genlnf("}");
            }
        }
        if (stmt->if_stmt.init) {
            gen_indent--;
            genlnf("}");
        }
        break;
    case STMT_WHILE:
        genlnf("while (");
        gen_expr(stmt->while_stmt.cond);
        genf(") ");
        gen_stmt_block(stmt->while_stmt.block);
        break;
    case STMT_DO_WHILE:
        genlnf("do ");
        gen_stmt_block(stmt->while_stmt.block);
        genf(" while (");
        gen_expr(stmt->while_stmt.cond);
        genf(");");
        break;
    case STMT_FOR:
        genlnf("for (");
        if (stmt->for_stmt.init) {
            gen_simple_stmt(stmt->for_stmt.init);
        }
        genf(";");
        if (stmt->for_stmt.cond) {
            genf(" ");
            gen_expr(stmt->for_stmt.cond);
        }
        genf(";");
        if (stmt->for_stmt.next) {
            genf(" ");
            gen_simple_stmt(stmt->for_stmt.next);
        }
        genf(") ");
        gen_stmt_block(stmt->for_stmt.block);
        break;
    case STMT_SWITCH: {
        genlnf("switch (");
        gen_expr(stmt->switch_stmt.expr);
        genf(") {");
        bool has_default = false;
        for (size_t i = 0; i < stmt->switch_stmt.num_cases; i++) {
            SwitchCase switch_case = stmt->switch_stmt.cases[i];
            for (size_t j = 0; j < switch_case.num_patterns; j++) {
                SwitchCasePattern pattern = switch_case.patterns[j];
                if (pattern.end) {
                    Val start_val = get_resolved_val(pattern.start);
                    Val end_val = get_resolved_val(pattern.end);
                    if (is_char_lit(pattern.start) && is_char_lit(pattern.end)) {
                        genln();
                        for (int c = (int)start_val.ll; c <= (int)end_val.ll; c++) {
                            genf("case ");
                            gen_char(c);
                            genf(": ");
                        } 
                    } else {
                        genlnf("// ");
                        gen_expr(pattern.start);
                        genf("...");
                        gen_expr(pattern.end);
                        genln();
                        for (long long ll = start_val.ll; ll <= end_val.ll; ll++) {
                            genf("case %lld: ", ll);
                        }
                    }
                } else {
                    genlnf("case ");
                    gen_expr(pattern.start);
                    genf(":");
                }
            }
            if (switch_case.is_default) {
                has_default = true;
                genlnf("default:");
            }
            genf(" ");
            genf("{");
            gen_indent++;
            StmtList block = switch_case.block;
            for (size_t j = 0; j < block.num_stmts; j++) {
                gen_stmt(block.stmts[j]);
            }
            genlnf("break;");
            gen_indent--;
            genlnf("}");
        }
        if (!has_default) {
            Note *note = get_stmt_note(stmt, complete_name);
            if (note) {
                genlnf("default:");
                gen_indent++;
                genlnf("assert(\"@complete switch failed to handle case\" && 0);");
                genlnf("break;");
                gen_indent--;
            }
        }
        genlnf("}");
        break;
    }
    case STMT_LABEL:
        genlnf("%s: ;", stmt->label);
        break;
    case STMT_GOTO:
        genlnf("goto %s;", stmt->label);
        break;
    default:
        genln();
        gen_simple_stmt(stmt);
        genf(";");
        break;
    }
}

void gen_decl(Sym *sym) {
    Decl *decl = sym->decl;
    if (!decl || is_decl_foreign(decl)) {
        return;
    }
    gen_sync_pos(decl->pos);
    switch (decl->kind) {
    case DECL_CONST:
        genlnf("#define %s (", get_gen_name(sym));
        if (decl->const_decl.type) {
            genf("(%s)(", typespec_to_cdecl(decl->const_decl.type, ""));
        }
        gen_expr(decl->const_decl.expr);
        if (decl->const_decl.type) {
            genf(")");
        }
        genf(")");
        break;
    case DECL_VAR:
        if (is_decl_threadlocal(decl)) {
            genlnf("THREADLOCAL");
        }
        genlnf("extern ");
        if (decl->var.type && !is_incomplete_array_typespec(decl->var.type)) {
            genf("%s", typespec_to_cdecl(decl->var.type, get_gen_name(sym)));
        } else {
            genf("%s", type_to_cdecl(sym->type, get_gen_name(sym)));
        }
        genf(";");
        break;
    case DECL_FUNC:
        gen_func_decl(decl);
        genf(";");
        break;
    case DECL_STRUCT:
    case DECL_UNION:
        gen_aggregate(decl);
        break;
    case DECL_TYPEDEF:
        genlnf("typedef %s;", typespec_to_cdecl(decl->typedef_decl.type, get_gen_name(sym)));
        break;
    case DECL_ENUM:
        if (decl->enum_decl.type) {
            genlnf("typedef %s;", typespec_to_cdecl(decl->enum_decl.type, get_gen_name(decl)));
        } else {
            genlnf("typedef int %s;", get_gen_name(decl));
        }
        break;
    case DECL_IMPORT:
        // Do nothing
        break;
    default:
        assert(0);
        break;
    }
    genln();
}

void gen_sorted_decls(void) {
    for (int i = 0; i < buf_len(tuple_types); i++) {
        Type *type = tuple_types[i];
        if (!is_tuple_reachable(type)) {
            continue;
        }
        genlnf("struct tuple%d {", type->typeid);
        gen_indent++;
        for (size_t i = 0; i < type->aggregate.num_fields; i++) {
            TypeField field = type->aggregate.fields[i];
            genlnf("%s;", type_to_cdecl(field.type, field.name));
        }
        gen_indent--;
        genlnf("};");
    }
    for (size_t i = 0; i < buf_len(sorted_syms); i++) {
        if (sorted_syms[i]->reachable == REACHABLE_NATURAL) {
            gen_decl(sorted_syms[i]);
        }
    }
}

void gen_defs(void) {
    for (Sym **it = sorted_syms; it != buf_end(sorted_syms); it++) {
        Sym *sym = *it;
        Decl *decl = sym->decl;
        if (sym->state != SYM_RESOLVED || !decl || decl->is_incomplete || sym->reachable != REACHABLE_NATURAL) {
            continue;
        }
        if (decl->kind == DECL_FUNC) {
            bool foreign = is_decl_foreign(decl);;
            char *buf = gen_buf;
            if (foreign) {
                gen_buf = NULL;
            }
            if (get_decl_note(decl, inline_name)) {
                genlnf("INLINE");
            }
            if (get_decl_note(decl, str_intern("noinline"))) {
                genlnf("NOINLINE");
            }
            gen_func_decl(decl);
            genf(" ");
            gen_stmt_block(decl->func.block);
            genln();
            if (foreign) {
                gen_buf = buf;
            }
        } else if (decl->kind == DECL_VAR) {
            if (is_decl_threadlocal(decl)) {
                genlnf("THREADLOCAL");
            }
            if (decl->var.type && !is_incomplete_array_typespec(decl->var.type)) {
                genlnf("%s", typespec_to_cdecl(decl->var.type, get_gen_name(sym)));
            } else {
                genlnf("%s", type_to_cdecl(sym->type, get_gen_name(sym)));
            }
            if (decl->var.expr) {
                genf(" = ");
                gen_expr(decl->var.expr);
            }
            genf(";");
        }
    }
}

Map gen_foreign_headers_map;
const char **gen_foreign_headers_buf;

static void add_foreign_header(const char *name) {
    name = str_intern(name);
    if (!map_get(&gen_foreign_headers_map, name)) {
        map_put(&gen_foreign_headers_map, name, (void *)1);
        buf_push(gen_foreign_headers_buf, name);
    }
}

const char **gen_foreign_sources_buf;

static void add_foreign_source(const char *name) {
    buf_push(gen_foreign_sources_buf, str_intern(name));
}

void gen_include(const char *path) {
    genlnf("#include ");
    if (*path == '<') {
        genf("%s", path);
    } else {
        gen_str(path, false);
    }
}

void gen_foreign_headers(void) {
    if (gen_foreign_headers_buf) {
        for (size_t i = 0; i < buf_len(gen_foreign_headers_buf); i++) {
            gen_include(gen_foreign_headers_buf[i]);
        }
    }
}

void gen_foreign_sources(void) {
    for (size_t i = 0; i < buf_len(gen_foreign_sources_buf); i++) {
        gen_include(gen_foreign_sources_buf[i]);
    }
}

const char **gen_sources_buf;

static void put_include_path(char path[MAX_PATH], Package *package, const char *filename) {
    if (*filename == '<') {
        path_copy(path, filename);
    } else {
        path_copy(path, package->full_path);
        path_join(path, filename);
        path_absolute(path);
    }
}

static void preprocess_package(Package *package) {
    if (!package->external_name) {
        char *external_name = NULL;
        for (const char *ptr = package->path; *ptr; ptr++) {
            buf_printf(external_name, "%c", *ptr == '/' ? '_' : *ptr);
        }
        buf_printf(external_name, "_");
        package->external_name = str_intern(external_name);
    }
    const char *header_name = str_intern("header");
    const char *source_name = str_intern("source");
    const char *preamble_name = str_intern("preamble");
    const char *postamble_name = str_intern("postamble");
    for (size_t i = 0; i < package->num_decls; i++) {
        Decl *decl = package->decls[i];
        if (decl->kind != DECL_NOTE) {
            continue;
        }
        Note note = decl->note;
        if (note.name == foreign_name) {
            for (size_t k = 0; k < note.num_args; k++) {
                NoteArg arg = note.args[k];
                Expr *expr = note.args[k].expr;
                if (expr->kind != EXPR_STR) {
                    fatal_error(decl->pos, "#foreign argument must be a string");
                }
                const char *str = expr->str_lit.val;
                if (arg.name == header_name) {
                    char path[MAX_PATH];
                    put_include_path(path, package, str);
                    add_foreign_header(path);
                } else if (arg.name == source_name) {
                    char path[MAX_PATH];
                    put_include_path(path, package, str);
                    add_foreign_source(path);
                } else if (arg.name == preamble_name) {
                    gen_buf_pos(&gen_preamble_buf, arg.pos);
                    buf_printf(gen_preamble_buf, "%s\n", str);
                } else if (arg.name == postamble_name) {
                    gen_buf_pos(&gen_postamble_buf, arg.pos);
                    buf_printf(gen_postamble_buf, "%s\n", str);
                } else {
                    fatal_error(decl->pos, "Unknown #foreign named argument '%s'", arg.name);
                }
            }
        }
    }
}

void preprocess_packages(void) {
    for (size_t i = 0; i < buf_len(package_list); i++) {
        preprocess_package(package_list[i]);
    }
}

void gen_typeinfo_header(const char *kind, Type *type) { 
    if (type_sizeof(type) == 0) {
        genf("&(TypeInfo){%s, .size = 0, .align = 0", kind);
    } else {
        const char *ctype = type_to_cdecl(type, "");
        genf("&(TypeInfo){%s, .size = sizeof(%s), .align = alignof(%s)", kind, ctype, ctype);
    }
}

void gen_typeinfo_fields(Type *type) {
    gen_indent++;
    for (size_t i = 0; i < type->aggregate.num_fields; i++) {
        TypeField field = type->aggregate.fields[i];
        genlnf("{");
        gen_str(field.name, false);
        genf(", .type = ");
        gen_typeid(field.type);
        genf(", .offset = offsetof(%s, %s)},", get_gen_name(type->sym), field.name);
    }
    gen_indent--;
}

#define CASE(kind, name) \
    case kind: \
        genf("&(TypeInfo){" #kind ", .size = sizeof(" #name "), .align = sizeof(" #name "), .name = "); \
        gen_str(#name, false); \
        genf("},"); \
        break;

void gen_typeinfo(Type *type) {
    switch (type->kind) {
    CASE(TYPE_BOOL, bool)
    CASE(TYPE_CHAR, char)
    CASE(TYPE_UCHAR, uchar)
    CASE(TYPE_SCHAR, schar)
    CASE(TYPE_SHORT, short)
    CASE(TYPE_USHORT, ushort)
    CASE(TYPE_INT, int)
    CASE(TYPE_UINT, uint)
    CASE(TYPE_LONG, long)
    CASE(TYPE_ULONG, ulong)
    CASE(TYPE_LLONG, llong)
    CASE(TYPE_ULLONG, ullong)
    CASE(TYPE_FLOAT, float)
    CASE(TYPE_DOUBLE, double)
    case TYPE_VOID:
        genf("&(TypeInfo){TYPE_VOID, .name = \"void\", .size = 0, .align = 0},");
        break;
    case TYPE_PTR:
        genf("&(TypeInfo){TYPE_PTR, .size = sizeof(void *), .align = alignof(void *), .base = ");
        gen_typeid(type->base);
        genf("},");
        break;
    case TYPE_CONST:
        gen_typeinfo_header("TYPE_CONST", type);
        genf(", .base = ");
        gen_typeid(type->base);
        genf("},");
        break;
    case TYPE_ARRAY:
        if (is_incomplete_array_type(type)) {
            genf("NULL, // Incomplete array type");
        } else {
            gen_typeinfo_header("TYPE_ARRAY", type);
            genf(", .base = ");
            gen_typeid(type->base);
            genf(", .count = %d},", type->num_elems);
        }
        break;
    case TYPE_STRUCT:
    case TYPE_UNION:
        gen_typeinfo_header(type->kind == TYPE_STRUCT ? "TYPE_STRUCT" : "TYPE_UNION", type);
        genf(", .name = ");
        gen_str(get_gen_name(type->sym), false);
        genf(", .num_fields = %d, .fields = (TypeFieldInfo[]) {", type->aggregate.num_fields);
        gen_typeinfo_fields(type);
        genlnf("}},");
        break;
    case TYPE_FUNC:
        genf("NULL, // Func");
        break;
    case TYPE_ENUM:
        genf("NULL, // Enum");
        break;
    case TYPE_INCOMPLETE:
        genf("NULL, // Incomplete: %s", get_gen_name(type->sym));
        break;
    default:
        genf("NULL, // Unhandled");
        break;
    }
}

#undef CASE

void gen_typeinfos(void) {
    genlnf("#define TYPEID0(index, kind) ((ullong)(index) | ((ullong)(kind) << 24))");
    genlnf("#define TYPEID(index, kind, ...) ((ullong)(index) | ((ullong)sizeof(__VA_ARGS__) << 32) | ((ullong)(kind) << 24))");
    genln();
    if (flag_notypeinfo) {
        genlnf("int num_typeinfos;");
        genlnf("TypeInfo **typeinfos;");
    } else {
        int num_typeinfos = next_typeid;
        genlnf("TypeInfo *typeinfo_table[%d] = {", num_typeinfos);
        gen_indent++;
        for (int typeid = 0; typeid < num_typeinfos; typeid++) {
            genlnf("[%d] = ", typeid);
            Type *type = get_type_from_typeid(typeid);
            if (type && !is_excluded_typeinfo(type)) {
                gen_typeinfo(type);
            } else {
                genf("NULL, // No associated type");
            }
        }
        gen_indent--;
        genlnf("};");
        genln();
        genlnf("int num_typeinfos = %d;", num_typeinfos);
        genlnf("TypeInfo **typeinfos = (TypeInfo **)typeinfo_table;");
    }
}

void gen_package_external_names(void) {
    for (size_t i = 0; i < buf_len(package_list); i++) {
    }
}

void gen_preamble(void) {
    if (gen_preamble_buf) {
        genlnf("%s", gen_preamble_buf);
    }
}

void gen_postamble(void) {
    if (gen_postamble_buf) {
        genlnf("%s", gen_postamble_buf);
    }
}

void gen_all(void) {
    preprocess_packages();
    gen_buf = NULL;
    gen_foreign_headers();
    genln();
    gen_forward_decls();
    genln();
    gen_sorted_decls();
    gen_typeinfos();
    gen_defs();
    gen_foreign_sources();
    genln();
    gen_postamble();
    char *buf = gen_buf;
    gen_buf = NULL;
    gen_preamble();
    genf("%s", buf);
}
