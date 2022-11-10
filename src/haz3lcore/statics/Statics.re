open Sexplib.Std;

/* STATICS

     This module determines the statics semantics of the language.
     It takes a term and returns a map which associates the unique
     ids of each term to an 'info' data structure which reflects that
     term's statics. The statics collected depend on the term's sort,
     but every term has a syntactic class (The cls types from Term),
     except Invalid terms which Term could not parse.

     The map generated by this module is intended to be generated once
     from a given term and then reused anywhere there is logic which
     depends on static information.
   */

/* Expressions are assigned a mode (reflecting the static expectations
   if any of their syntactic parent), a self (reflecting what their
   statics would be in isolation), a context (variables in scope), and
   free (variables occuring free in the expression. */
[@deriving (show({with_path: false}), sexp, yojson)]
type info_exp = {
  cls: Term.UExp.cls,
  term: Term.UExp.t,
  mode: Typ.mode,
  self: Typ.self,
  ctx: Ctx.t,
  free: Ctx.co,
  // TODO: add derived attributes like error_status and typ_after_fix?
};

/* Patterns are assigned a mode (reflecting the static expectations
   if any of their syntactic parent) and a self (reflecting what their
   statics would be in isolation), a context (variables in scope) */
[@deriving (show({with_path: false}), sexp, yojson)]
type info_pat = {
  cls: Term.UPat.cls,
  term: Term.UPat.t,
  mode: Typ.mode,
  self: Typ.self,
  ctx: Ctx.t // TODO: detect in-pattern shadowing
};

/* (Syntactic) Types are assigned their corresponding semantic type. */
[@deriving (show({with_path: false}), sexp, yojson)]
type info_typ = {
  cls: Term.UTyp.cls,
  term: Term.UTyp.t,
  self: Typ.self,
};

[@deriving (show({with_path: false}), sexp, yojson)]
type info_rul = {
  cls: Term.URul.cls,
  term: Term.UExp.t,
};

[@deriving (show({with_path: false}), sexp, yojson)]
type info_tpat = {
  cls: Term.UTPat.cls,
  term: Term.UTPat.t,
};

[@deriving (show({with_path: false}), sexp, yojson)]
type info_tsum = {
  cls: Term.UTSum.cls,
  term: Term.UTSum.t,
};

/* The Info aka Cursorinfo assigned to each subterm. */
[@deriving (show({with_path: false}), sexp, yojson)]
type t =
  | Invalid(TermBase.parse_flag)
  | InfoExp(info_exp)
  | InfoPat(info_pat)
  | InfoTyp(info_typ)
  | InfoRul(info_rul)
  | InfoTPat(info_tpat)
  | InfoTSum(info_tsum);

/* The InfoMap collating all info for a composite term */
type map = Id.Map.t(t);

let terms = (map: map): Id.Map.t(Term.any) =>
  map
  |> Id.Map.filter_map(_ =>
       fun
       | Invalid(_) => None
       | InfoExp({term, _}) => Some(Term.Exp(term))
       | InfoPat({term, _}) => Some(Term.Pat(term))
       | InfoTyp({term, _}) => Some(Term.Typ(term))
       | InfoRul({term, _}) => Some(Term.Exp(term))
       | InfoTPat({term, _}) => Some(Term.TPat(term))
       | InfoTSum({term, _}) => Some(Term.TSum(term))
     );

/* TODO(andrew): more sum/rec errors
   incomplete type (holes)?
   empty / singleton sum?
   duplicate constructor?
   */

/* Static error classes */
[@deriving (show({with_path: false}), sexp, yojson)]
type error =
  | Free(Typ.free_errors)
  | Multi
  | SynInconsistentBranches(list(Typ.t))
  | TypeInconsistent(Typ.t, Typ.t);

/* Statics non-error classes */
[@deriving (show({with_path: false}), sexp, yojson)]
type happy =
  | SynConsistent(Typ.t)
  | AnaConsistent(Typ.t, Typ.t, Typ.t) //ana, syn, join
  | AnaInternalInconsistent(Typ.t, list(Typ.t)) // ana, branches
  | AnaExternalInconsistent(Typ.t, Typ.t); // ana, syn

/* The error status which 'wraps' each term. */
[@deriving (show({with_path: false}), sexp, yojson)]
type error_status =
  | InHole(error)
  | NotInHole(happy);

/* Determines whether an expression or pattern is in an error hole,
   depending on the mode, which represents the expectations of the
   surrounding syntactic context, and the self which represents the
   makeup of the expression / pattern itself. */
let error_status = (ctx: Ctx.t, mode: Typ.mode, self: Typ.self): error_status =>
  switch (mode, self) {
  | (Syn | Ana(_), Free(free_error)) => InHole(Free(free_error))
  | (Syn | Ana(_), Multi) => NotInHole(SynConsistent(Unknown(Internal)))
  | (Syn, Just(ty)) => NotInHole(SynConsistent(ty))
  | (Syn, Joined(wrap, tys_syn)) =>
    /*| (Ana(Unknown(SynSwitch)), Joined(tys_syn))*/
    // Above can be commented out if we actually switch to syn on synswitch
    let tys_syn = Typ.source_tys(tys_syn);
    switch (Ctx.join_all(ctx, tys_syn)) {
    | None => InHole(SynInconsistentBranches(tys_syn))
    | Some(ty_joined) => NotInHole(SynConsistent(wrap(ty_joined)))
    };

  | (Ana(ty_ana), Just(ty_syn)) =>
    switch (Ctx.join(ctx, ty_ana, ty_syn)) {
    | None => InHole(TypeInconsistent(ty_syn, ty_ana))
    | Some(ty_join) => NotInHole(AnaConsistent(ty_ana, ty_syn, ty_join))
    }
  | (Ana(ty_ana), Joined(wrap, tys_syn)) =>
    // TODO: review logic of these cases
    switch (Ctx.join_all(ctx, Typ.source_tys(tys_syn))) {
    | Some(ty_syn) =>
      let ty_syn = wrap(ty_syn);
      switch (Ctx.join(ctx, ty_syn, ty_ana)) {
      | None => NotInHole(AnaExternalInconsistent(ty_ana, ty_syn))
      | Some(ty_join) => NotInHole(AnaConsistent(ty_ana, ty_syn, ty_join))
      };
    | None =>
      NotInHole(AnaInternalInconsistent(ty_ana, Typ.source_tys(tys_syn)))
    }
  };

/* Determines whether any term is in an error hole. Currently types cannot
   be in error, and Invalids (things to which Term was unable to assign a
   parse) are always in error. The error status of expressions and patterns
   are determined by error_status above. */
let is_error = (ci: t): bool => {
  switch (ci) {
  | Invalid(Whitespace) => false
  | Invalid(_) => true
  | InfoExp({mode, self, ctx, _})
  | InfoPat({mode, self, ctx, _}) =>
    switch (error_status(ctx, mode, self)) {
    | InHole(_) => true
    | NotInHole(_) => false
    }
  | InfoTyp({self, _}) =>
    switch (self) {
    | Free(TypeVariable) => true
    | _ => false
    }
  | InfoRul(_) => false
  | InfoTSum(_) => false //TODO(andrew): TSum errors?
  | InfoTPat(_) => false //TODO(andrew): TPat errors?
  };
};

/* Determined the type of an expression or pattern 'after hole wrapping';
   that is, all ill-typed terms are considered to be 'wrapped in
   non-empty holes', i.e. assigned Unknown type. */
let typ_after_fix = (ctx, mode: Typ.mode, self: Typ.self): Typ.t =>
  switch (error_status(ctx, mode, self)) {
  | InHole(_) => Unknown(Internal)
  | NotInHole(SynConsistent(t)) => t
  | NotInHole(AnaConsistent(_, _, ty_join)) => ty_join
  | NotInHole(AnaExternalInconsistent(ty_ana, _)) => ty_ana
  | NotInHole(AnaInternalInconsistent(ty_ana, _)) => ty_ana
  };

/* The type of an expression after hole wrapping */
let exp_typ = (ctx, m: map, e: Term.UExp.t): Typ.t =>
  switch (Id.Map.find_opt(Term.UExp.rep_id(e), m)) {
  | Some(InfoExp({mode, self, _})) => typ_after_fix(ctx, mode, self)
  | Some(
      InfoPat(_) | InfoTyp(_) | InfoRul(_) | InfoTPat(_) | InfoTSum(_) |
      Invalid(_),
    )
  | None => failwith(__LOC__ ++ ": XXX")
  };

let t_of_self = (ctx): (Typ.self => Typ.t) =>
  fun
  | Just(t) => t
  | Joined(wrap, ss) =>
    switch (ss |> List.map((s: Typ.source) => s.ty) |> Ctx.join_all(ctx)) {
    | None => Unknown(Internal)
    | Some(t) => wrap(t)
    }
  | Multi
  | Free(_) => Unknown(Internal);

let exp_self_typ_id = (ctx, m: map, id): Typ.t =>
  switch (Id.Map.find_opt(id, m)) {
  | Some(InfoExp({self, _})) => t_of_self(ctx, self)
  | Some(
      InfoPat(_) | InfoTyp(_) | InfoRul(_) | InfoTPat(_) | InfoTSum(_) |
      Invalid(_),
    )
  | None => failwith(__LOC__ ++ ": XXX")
  };

let exp_self_typ = (ctx, m: map, e: Term.UExp.t): Typ.t =>
  exp_self_typ_id(ctx, m, Term.UExp.rep_id(e));

let exp_mode_id = (m: map, id): Typ.mode =>
  switch (Id.Map.find_opt(id, m)) {
  | Some(InfoExp({mode, _})) => mode
  | Some(
      InfoPat(_) | InfoTyp(_) | InfoRul(_) | InfoTPat(_) | InfoTSum(_) |
      Invalid(_),
    )
  | None => failwith(__LOC__ ++ ": XXX")
  };

let exp_mode = (m: map, e: Term.UExp.t): Typ.mode =>
  exp_mode_id(m, Term.UExp.rep_id(e));

/* The type of a pattern after hole wrapping */
let pat_typ = (ctx, m: map, p: Term.UPat.t): Typ.t =>
  switch (Id.Map.find_opt(Term.UPat.rep_id(p), m)) {
  | Some(InfoPat({mode, self, _})) => typ_after_fix(ctx, mode, self)
  | Some(
      InfoExp(_) | InfoTyp(_) | InfoRul(_) | InfoTPat(_) | InfoTSum(_) |
      Invalid(_),
    )
  | None => failwith(__LOC__ ++ ": XXX")
  };
let pat_self_typ = (ctx, m: map, p: Term.UPat.t): Typ.t =>
  switch (Id.Map.find_opt(Term.UPat.rep_id(p), m)) {
  | Some(InfoPat({self, _})) => t_of_self(ctx, self)
  | Some(
      InfoExp(_) | InfoTyp(_) | InfoRul(_) | InfoTPat(_) | InfoTSum(_) |
      Invalid(_),
    )
  | None => failwith(__LOC__ ++ ": XXX")
  };

// NOTE(andrew): changed this from union to disj_union...
let union_m = List.fold_left(Id.Map.disj_union, Id.Map.empty);

let add_info = (ids, info: 'a, m: Ptmap.t('a)) =>
  ids
  |> List.map(id => Id.Map.singleton(id, info))
  |> List.fold_left(Id.Map.disj_union, m);

let extend_let_def_ctx =
    (ctx: Ctx.t, pat: Term.UPat.t, pat_ctx: Ctx.t, def: Term.UExp.t) =>
  if (Term.UPat.is_tuple_of_arrows(pat)
      && Term.UExp.is_tuple_of_functions(def)) {
    pat_ctx;
  } else {
    ctx;
  };

let typ_exp_binop_bin_int: Term.UExp.op_bin_int => Typ.t =
  fun
  | (Plus | Minus | Times | Divide) as _op => Int
  | (LessThan | GreaterThan | LessThanOrEqual | GreaterThanOrEqual | Equals) as _op =>
    Bool;

let typ_exp_binop_bin_float: Term.UExp.op_bin_float => Typ.t =
  fun
  | (Plus | Minus | Times | Divide) as _op => Float
  | (LessThan | GreaterThan | LessThanOrEqual | GreaterThanOrEqual | Equals) as _op =>
    Bool;

let typ_exp_binop_bin_string: Term.UExp.op_bin_string => Typ.t =
  fun
  | Equals as _op => Bool;

let typ_exp_binop: Term.UExp.op_bin => (Typ.t, Typ.t, Typ.t) =
  fun
  | Bool(And | Or) => (Bool, Bool, Bool)
  | Int(op) => (Int, Int, typ_exp_binop_bin_int(op))
  | Float(op) => (Float, Float, typ_exp_binop_bin_float(op))
  | String(op) => (String, String, typ_exp_binop_bin_string(op));

let typ_exp_unop: Term.UExp.op_un => (Typ.t, Typ.t) =
  fun
  | Int(Minus) => (Int, Int);

let rec any_to_info_map = (~ctx: Ctx.t, any: Term.any): (Ctx.co, map) =>
  switch (any) {
  | Exp(e) =>
    let (_, co, map) = uexp_to_info_map(~ctx, e);
    (co, map);
  | Pat(p) =>
    let (_, _, map) = upat_to_info_map(~ctx, p);
    (VarMap.empty, map);
  | TSum(_) => (VarMap.empty, Ptmap.empty) //TODO(andrew)
  | TPat(tp) =>
    let map = utpat_to_info_map(~ctx, tp);
    (VarMap.empty, map);
  | Typ(ty) =>
    let (_, map) = utyp_to_info_map(~ctx, ty);
    (VarMap.empty, map);
  // TODO(d) consider Rul case
  | Rul(_)
  | Nul ()
  | Any () => (VarMap.empty, Id.Map.empty)
  }
and uexp_to_info_map =
    (~ctx: Ctx.t, ~mode=Typ.Syn, {ids, term} as uexp: Term.UExp.t)
    : (Typ.t, Ctx.co, map) => {
  /* Maybe switch mode to syn */
  let mode =
    switch (mode) {
    | Ana(Unknown(SynSwitch)) => Typ.Syn
    | _ => mode
    };
  let cls = Term.UExp.cls_of_term(term);
  let go = uexp_to_info_map(~ctx);
  let add = (~self, ~free, m) => (
    typ_after_fix(ctx, mode, self),
    free,
    add_info(ids, InfoExp({cls, self, mode, ctx, free, term: uexp}), m),
  );
  let atomic = self => add(~self, ~free=[], Id.Map.empty);
  switch (term) {
  | Invalid(msg) => (
      Unknown(Internal),
      [],
      add_info(ids, Invalid(msg), Id.Map.empty),
    )
  | MultiHole(tms) =>
    let (free, maps) = tms |> List.map(any_to_info_map(~ctx)) |> List.split;
    add(~self=Multi, ~free=Ctx.union(free), union_m(maps));
  | EmptyHole => atomic(Just(Unknown(Internal)))
  | Triv => atomic(Just(Prod([])))
  | Bool(_) => atomic(Just(Bool))
  | Int(_) => atomic(Just(Int))
  | Float(_) => atomic(Just(Float))
  | String(_) => atomic(Just(String))
  | Var(name) =>
    switch (Ctx.lookup_var(ctx, name)) {
    | None => atomic(Free(Variable))
    | Some(typ) =>
      add(
        ~self=Just(typ),
        ~free=[(name, [{id: Term.UExp.rep_id(uexp), mode}])],
        Id.Map.empty,
      )
    }
  | Parens(e) =>
    let (ty, free, m) = go(~mode, e);
    add(~self=Just(ty), ~free, m);
  | UnOp(op, e) =>
    let (ty_in, ty_out) = typ_exp_unop(op);
    let (_, free, m) = go(~mode=Ana(ty_in), e);
    add(~self=Just(ty_out), ~free, m);
  | BinOp(op, e1, e2) =>
    let (ty1, ty2, ty_out) = typ_exp_binop(op);
    let (_, free1, m1) = go(~mode=Ana(ty1), e1);
    let (_, free2, m2) = go(~mode=Ana(ty2), e2);
    add(
      ~self=Just(ty_out),
      ~free=Ctx.union([free1, free2]),
      union_m([m1, m2]),
    );
  | Tuple(es) =>
    let modes = Typ.matched_prod_mode(mode, List.length(es));
    let infos = List.map2((e, mode) => go(~mode, e), es, modes);
    let free = Ctx.union(List.map(((_, f, _)) => f, infos));
    let self = Typ.Just(Prod(List.map(((ty, _, _)) => ty, infos)));
    let m = union_m(List.map(((_, _, m)) => m, infos));
    add(~self, ~free, m);
  | Tag(tag) =>
    switch (Ctx.lookup_tag(ctx, tag)) {
    | Some(typ) => atomic(Just(typ))
    | None => atomic(Free(Tag))
    }
  | Cons(e1, e2) =>
    let mode_ele = Typ.matched_list_mode(mode);
    let (ty1, free1, m1) = go(~mode=mode_ele, e1);
    let (_, free2, m2) = go(~mode=Ana(List(ty1)), e2);
    add(
      ~self=Just(List(ty1)),
      ~free=Ctx.union([free1, free2]),
      union_m([m1, m2]),
    );
  | ListLit([]) => atomic(Just(List(Unknown(Internal))))
  | ListLit(es) =>
    let modes = Typ.matched_list_lit_mode(mode, List.length(es));
    let e_ids = List.map(Term.UExp.rep_id, es);
    let infos = List.map2((e, mode) => go(~mode, e), es, modes);
    let tys = List.map(((ty, _, _)) => ty, infos);
    let self: Typ.self =
      switch (Ctx.join_all(ctx, tys)) {
      | None =>
        Joined(
          ty => List(ty),
          List.map2((id, ty) => Typ.{id, ty}, e_ids, tys),
        )
      | Some(ty) => Just(List(ty))
      };
    let free = Ctx.union(List.map(((_, f, _)) => f, infos));
    let m = union_m(List.map(((_, _, m)) => m, infos));
    add(~self, ~free, m);
  | Test(test) =>
    let (_, free_test, m1) = go(~mode=Ana(Bool), test);
    add(~self=Just(Prod([])), ~free=free_test, m1);
  | If(cond, e1, e2) =>
    let (_, free_e0, m1) = go(~mode=Ana(Bool), cond);
    let (ty_e1, free_e1, m2) = go(~mode, e1);
    let (ty_e2, free_e2, m3) = go(~mode, e2);
    add(
      ~self=
        Joined(
          Fun.id,
          [
            {id: Term.UExp.rep_id(e1), ty: ty_e1},
            {id: Term.UExp.rep_id(e2), ty: ty_e2},
          ],
        ),
      ~free=Ctx.union([free_e0, free_e1, free_e2]),
      union_m([m1, m2, m3]),
    );
  | Seq(e1, e2) =>
    let (_, free1, m1) = go(~mode=Syn, e1);
    let (ty2, free2, m2) = go(~mode, e2);
    add(
      ~self=Just(ty2),
      ~free=Ctx.union([free1, free2]),
      union_m([m1, m2]),
    );
  | Ap(fn, arg) =>
    /* Function position mode Ana(Hole->Hole) instead of Syn */
    let (ty_fn, free_fn, m_fn) =
      uexp_to_info_map(~ctx, ~mode=Typ.ap_mode, fn);
    let (ty_in, ty_out) = Typ.matched_arrow(ty_fn);
    let (_, free_arg, m_arg) =
      uexp_to_info_map(~ctx, ~mode=Ana(ty_in), arg);
    add(
      ~self=Just(ty_out),
      ~free=Ctx.union([free_fn, free_arg]),
      union_m([m_fn, m_arg]),
    );
  | Fun(pat, body) =>
    let (mode_pat, mode_body) = Typ.matched_arrow_mode(mode);
    let (ty_pat, ctx_pat, m_pat) =
      upat_to_info_map(~ctx, ~mode=mode_pat, pat);
    let (ty_body, free_body, m_body) =
      uexp_to_info_map(~ctx=ctx_pat, ~mode=mode_body, body);
    add(
      ~self=Just(Arrow(ty_pat, ty_body)),
      ~free=Ctx.subtract_typ(ctx_pat, free_body), // TODO: free may not be accurate since ctx now threaded through pat
      union_m([m_pat, m_body]),
    );
  | Let(pat, def, body) =>
    let (ty_pat, ctx_pat, _m_pat) = upat_to_info_map(~ctx, ~mode=Syn, pat);
    let def_ctx = extend_let_def_ctx(ctx, pat, ctx_pat, def);
    let (ty_def, free_def, m_def) =
      uexp_to_info_map(~ctx=def_ctx, ~mode=Ana(ty_pat), def);
    /* Analyze pattern to incorporate def type into ctx */
    let (_, ctx_pat_ana, m_pat) =
      upat_to_info_map(~ctx, ~mode=Ana(ty_def), pat);
    let (ty_body, free_body, m_body) =
      uexp_to_info_map(~ctx=ctx_pat_ana, ~mode, body);
    add(
      ~self=Just(ty_body),
      ~free=Ctx.union([free_def, Ctx.subtract_typ(ctx_pat_ana, free_body)]), // TODO: free may not be accurate since ctx now threaded through pat
      union_m([m_pat, m_def, m_body]),
    );
  | TyAlias({term: Var(name), _} as typat, utyp, body) =>
    let m_typat = utpat_to_info_map(~ctx, typat);
    let ty = Term.utyp_to_ty(utyp);
    let ty = List.mem(name, Typ.free_vars(ty)) ? Typ.Rec(name, ty) : ty;
    let ctx_def_and_body =
      Ctx.extend(
        TVarEntry({
          name,
          id: Term.UTPat.rep_id(typat),
          kind: Singleton(Term.utyp_to_ty(utyp)),
        }),
        ctx,
      );
    let (ty_body, free, m_body) =
      uexp_to_info_map(~ctx=ctx_def_and_body, ~mode, body);
    let (_ty_def, m_typ) = utyp_to_info_map(~ctx=ctx_def_and_body, utyp);
    add(~self=Just(ty_body), ~free, union_m([m_typat, m_body, m_typ]));
  | TyAlias(typat, _, e) =>
    //TODO(andrew): cleanup
    let m_typat = utpat_to_info_map(~ctx, typat);
    let (ty, free, m_body) = go(~mode, e);
    add(~self=Just(ty), ~free, union_m([m_typat, m_body]));
  | Match(scrut, rules) =>
    let (ty_scrut, free_scrut, m_scrut) = go(~mode=Syn, scrut);
    let (pats, branches) = List.split(rules);
    let pat_infos =
      List.map(upat_to_info_map(~ctx, ~mode=Typ.Ana(ty_scrut)), pats);
    let branch_infos =
      List.map2(
        (branch, (_, ctx_pat, _)) =>
          uexp_to_info_map(~ctx=ctx_pat, ~mode, branch),
        branches,
        pat_infos,
      );
    let branch_sources =
      List.map2(
        (e: Term.UExp.t, (ty, _, _)) => Typ.{id: Term.UExp.rep_id(e), ty},
        branches,
        branch_infos,
      );
    let pat_ms = List.map(((_, _, m)) => m, pat_infos);
    let branch_ms = List.map(((_, _, m)) => m, branch_infos);
    let branch_frees = List.map(((_, free, _)) => free, branch_infos);
    let self = Typ.Joined(Fun.id, branch_sources);
    let free = Ctx.union([free_scrut] @ branch_frees);
    add(~self, ~free, union_m([m_scrut] @ pat_ms @ branch_ms));
  };
}
and upat_to_info_map =
    (~ctx, ~mode: Typ.mode=Typ.Syn, {ids, term} as upat: Term.UPat.t)
    : (Typ.t, Ctx.t, map) => {
  let cls = Term.UPat.cls_of_term(term);
  let add = (~self, ~ctx, m) => (
    typ_after_fix(ctx, mode, self),
    ctx,
    add_info(ids, InfoPat({cls, self, mode, ctx, term: upat}), m),
  );
  let atomic = self => add(~self, ~ctx, Id.Map.empty);
  let unknown = Typ.Just(Unknown(SynSwitch));
  switch (term) {
  | Invalid(msg) => (
      Unknown(Internal),
      ctx,
      add_info(ids, Invalid(msg), Id.Map.empty),
    )
  | MultiHole(tms) =>
    let (_, maps) = tms |> List.map(any_to_info_map(~ctx)) |> List.split;
    add(~self=Multi, ~ctx, union_m(maps));
  | EmptyHole
  | Wild => atomic(unknown)
  | Int(_) => atomic(Just(Int))
  | Float(_) => atomic(Just(Float))
  | Triv => atomic(Just(Prod([])))
  | Bool(_) => atomic(Just(Bool))
  | String(_) => atomic(Just(String))
  | ListLit([]) => atomic(Just(List(Unknown(Internal))))
  | ListLit(ps) =>
    let modes = Typ.matched_list_lit_mode(mode, List.length(ps));
    let p_ids = List.map(Term.UPat.rep_id, ps);
    let (ctx, infos) =
      List.fold_left2(
        ((ctx, infos), e, mode) => {
          let (_, ctx, _) as info = upat_to_info_map(~mode, ~ctx, e);
          (ctx, infos @ [info]);
        },
        (ctx, []),
        ps,
        modes,
      );
    let tys = List.map(((ty, _, _)) => ty, infos);
    let self: Typ.self =
      switch (Ctx.join_all(ctx, tys)) {
      | None =>
        Joined(
          ty => List(ty),
          List.map2((id, ty) => Typ.{id, ty}, p_ids, tys),
        )
      | Some(ty) => Just(List(ty))
      };
    let info: t = InfoPat({cls, self, mode, ctx, term: upat});
    let m = union_m(List.map(((_, _, m)) => m, infos));
    /* Add an entry for the id of each comma tile */
    let m = List.fold_left((m, id) => Id.Map.add(id, info, m), m, ids);
    (typ_after_fix(ctx, mode, self), ctx, m);
  | Cons(hd, tl) =>
    let mode_elem = Typ.matched_list_mode(mode);
    let (ty, ctx, m_hd) = upat_to_info_map(~ctx, ~mode=mode_elem, hd);
    let (_, ctx, m_tl) = upat_to_info_map(~ctx, ~mode=Ana(List(ty)), tl);
    add(~self=Just(List(ty)), ~ctx, union_m([m_hd, m_tl]));
  | Tag(tag) =>
    switch (Ctx.lookup_tag(ctx, tag)) {
    | Some(typ) => atomic(Just(typ))
    | None => atomic(Free(Tag))
    }
  | Var(name) =>
    let self = unknown;
    let typ = typ_after_fix(ctx, mode, self);
    add(
      ~self,
      ~ctx=
        Ctx.extend(VarEntry({name, id: Term.UPat.rep_id(upat), typ}), ctx),
      Id.Map.empty,
    );
  | Tuple(ps) =>
    let modes = Typ.matched_prod_mode(mode, List.length(ps));
    let (ctx, infos) =
      List.fold_left2(
        ((ctx, infos), e, mode) => {
          let (_, ctx, _) as info = upat_to_info_map(~mode, ~ctx, e);
          (ctx, infos @ [info]);
        },
        (ctx, []),
        ps,
        modes,
      );
    let self = Typ.Just(Prod(List.map(((ty, _, _)) => ty, infos)));
    let m = union_m(List.map(((_, _, m)) => m, infos));
    add(~self, ~ctx, m);
  | Parens(p) =>
    let (ty, ctx, m) = upat_to_info_map(~ctx, ~mode, p);
    add(~self=Just(ty), ~ctx, m);
  | Ap(fn, arg) =>
    /* Contructor application */
    /* Function position mode Ana(Hole->Hole) instead of Syn */
    let (ty_fn, ctx, m_fn) = upat_to_info_map(~ctx, ~mode=Typ.ap_mode, fn);
    let (ty_in, ty_out) = Typ.matched_arrow(ty_fn);
    let (_, ctx, m_arg) = upat_to_info_map(~ctx, ~mode=Ana(ty_in), arg);
    add(~self=Just(ty_out), ~ctx, union_m([m_fn, m_arg]));
  | TypeAnn(p, ty) =>
    let (ty_ann, m_typ) = utyp_to_info_map(~ctx, ty);
    let (_ty, ctx, m) = upat_to_info_map(~ctx, ~mode=Ana(ty_ann), p);
    add(~self=Just(ty_ann), ~ctx, union_m([m, m_typ]));
  };
}
and utyp_to_info_map =
    (~ctx, {ids, term} as utyp: Term.UTyp.t): (Typ.t, map) => {
  let cls = Term.UTyp.cls_of_term(term);
  let ty = Term.utyp_to_ty(utyp);
  let add = self => add_info(ids, InfoTyp({cls, self, term: utyp}));
  let just = m => (ty, add(Just(ty), m));
  //TODO(andrew): make this return free, replacing Typ.free_vars
  switch (term) {
  | Invalid(msg) => (
      Unknown(Internal),
      add_info(ids, Invalid(msg), Id.Map.empty),
    )
  | EmptyHole
  | Int
  | Float
  | Bool
  | String => just(Id.Map.empty)
  | Sum({term, ids: _}) =>
    let m = utsum_to_info_map(~ctx, TermBase.UTSum.{term, ids: []});
    just(m);
  | List(t)
  | Parens(t) =>
    let (_, m) = utyp_to_info_map(~ctx, t);
    just(m);
  | Arrow(t1, t2) =>
    let (_, m_t1) = utyp_to_info_map(~ctx, t1);
    let (_, m_t2) = utyp_to_info_map(~ctx, t2);
    just(union_m([m_t1, m_t2]));
  | Tuple(ts) =>
    let m =
      ts |> List.map(utyp_to_info_map(~ctx)) |> List.map(snd) |> union_m;
    just(m);
  | Var(name) =>
    //TODO(andrew): better tvar lookup
    switch (List.assoc_opt(name, BuiltinADTs.adts)) {
    | None =>
      switch (Ctx.lookup_tvar(ctx, name)) {
      | None => (Unknown(Internal), add(Free(TypeVariable), Id.Map.empty))
      | Some(_) => (Var(name), add(Just(Var(name)), Id.Map.empty)) //TODO(andrew)
      }
    | Some(_) => (Var(name), add(Just(Var(name)), Id.Map.empty))
    }
  | MultiHole(tms) =>
    // TODO thread ctx through to multihole terms once ctx is available
    let (_, maps) =
      tms |> List.map(any_to_info_map(~ctx=Ctx.empty)) |> List.split;
    just(union_m(maps));
  };
}
and utpat_to_info_map = (~ctx as _, {ids, term} as utpat: Term.UTPat.t): map => {
  let cls = Term.UTPat.cls_of_term(term);
  add_info(ids, InfoTPat({cls, term: utpat}), Id.Map.empty);
}
and utsum_to_info_map = (~ctx, {ids, term} as utsum: Term.UTSum.t): map => {
  //TODO(andrew): check commented out below bits... triggers disj_union exceptions
  let cls = Term.UTSum.cls_of_term(term);
  let just = m => add_info(ids, InfoTSum({cls, term: utsum}), m);
  switch (term) {
  | Invalid(msg) => add_info(ids, Invalid(msg), Id.Map.empty)
  | EmptyHole => just(Id.Map.empty)
  | MultiHole(tms) =>
    let (_, maps) =
      tms |> List.map(any_to_info_map(~ctx=Ctx.empty)) |> List.split;
    just(union_m(maps));
  | Sum(sum) =>
    let ms = List.map(utsum_to_info_map(~ctx), sum);
    union_m(ms); //just(union_m(ms));
  | Ap(_, typ) =>
    let (_, m) = utyp_to_info_map(~ctx, typ);
    m; //just(m);
  };
};

let mk_map =
  Core.Memo.general(
    ~cache_size_bound=1000,
    e => {
      let (_, _, map) =
        uexp_to_info_map(~ctx=Builtins.ctx(Builtins.Pervasives.builtins), e);
      map;
    },
  );
