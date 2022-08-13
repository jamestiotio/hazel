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
  mode: Typ.mode,
  self: Typ.self,
  ctx: Ctx.t // TODO: detect in-pattern shadowing
};

/* (Syntactic) Types are assigned their corresponding semantic type. */
[@deriving (show({with_path: false}), sexp, yojson)]
type info_typ = {
  cls: Term.UTyp.cls,
  ty: Typ.t,
};

/* The Info aka Cursorinfo assigned to each subterm. */
[@deriving (show({with_path: false}), sexp, yojson)]
type t =
  | Invalid
  | InfoExp(info_exp)
  | InfoPat(info_pat)
  | InfoTyp(info_typ);

/* The InfoMap collating all info for a composite term */
type map = Id.Map.t(t);

/* Static error classes */
[@deriving (show({with_path: false}), sexp, yojson)]
type error =
  | FreeVariable
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
let error_status = (mode: Typ.mode, self: Typ.self): error_status =>
  switch (mode, self) {
  | (Syn | Ana(_), Free) => InHole(FreeVariable)
  | (Syn | Ana(_), Multi) => InHole(Multi)
  | (Syn, Just(ty)) => NotInHole(SynConsistent(ty))
  | (Syn, Joined(tys_syn))
  | (Ana(Unknown(SynSwitch)), Joined(tys_syn)) =>
    let tys_syn = Typ.source_tys(tys_syn);
    //TODO: clarify SynSwitch case
    switch (Typ.join_all(tys_syn)) {
    | None => InHole(SynInconsistentBranches(tys_syn))
    | Some(ty_joined) => NotInHole(SynConsistent(ty_joined))
    };
  | (Ana(ty_ana), Just(ty_syn)) =>
    switch (Typ.join(ty_ana, ty_syn)) {
    | None => InHole(TypeInconsistent(ty_syn, ty_ana))
    | Some(ty_join) => NotInHole(AnaConsistent(ty_ana, ty_syn, ty_join))
    }
  | (Ana(ty_ana), Joined(tys_syn)) =>
    // TODO: review logic of these cases
    switch (Typ.join_all(Typ.source_tys(tys_syn))) {
    | Some(ty_syn) =>
      switch (Typ.join(ty_syn, ty_ana)) {
      | None => NotInHole(AnaExternalInconsistent(ty_ana, ty_syn))
      | Some(ty_join) => NotInHole(AnaConsistent(ty_syn, ty_ana, ty_join))
      }
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
  | Invalid => true
  | InfoExp({mode, self, _})
  | InfoPat({mode, self, _}) =>
    switch (error_status(mode, self)) {
    | InHole(_) => true
    | NotInHole(_) => false
    }
  | InfoTyp(_) => false
  };
};

/* Determined the type of an expression or pattern 'after hole wrapping';
   that is, all ill-typed terms are considered to be 'wrapped in
   non-empty holes', i.e. assigned Unknown type. */
let typ_after_fix = (mode: Typ.mode, self: Typ.self): Typ.t =>
  switch (error_status(mode, self)) {
  | InHole(_) => Unknown(Internal)
  | NotInHole(SynConsistent(t)) => t
  | NotInHole(AnaConsistent(_, _, ty_join)) => ty_join
  | NotInHole(AnaExternalInconsistent(ty_ana, _)) => ty_ana
  | NotInHole(AnaInternalInconsistent(ty_ana, _)) => ty_ana
  };

/* The type of an expression after hole wrapping */
let exp_typ = (m: map, e: Term.UExp.t): Typ.t =>
  switch (Id.Map.find_opt(e.id, m)) {
  | Some(InfoExp({mode, self, _})) => typ_after_fix(mode, self)
  | Some(InfoPat(_) | InfoTyp(_) | Invalid)
  | None => failwith(__LOC__ ++ ": XXX")
  };

/* The type of a pattern after hole wrapping */
let pat_typ = (m: map, p: Term.UPat.t): Typ.t =>
  switch (Id.Map.find_opt(p.id, m)) {
  | Some(InfoPat({mode, self, _})) => typ_after_fix(mode, self)
  | Some(InfoExp(_) | InfoTyp(_) | Invalid)
  | None => failwith(__LOC__ ++ ": XXX")
  };

let union_m =
  List.fold_left(
    (m1, m2) => Id.Map.union((_, _, b) => Some(b), m1, m2),
    Id.Map.empty,
  );

let extend_let_def_ctx =
    (ctx: Ctx.t, pat: Term.UPat.t, def: Term.UExp.t, ty_ann: Typ.t) =>
  switch (ty_ann, pat.term, def.term) {
  | (Arrow(_), Var(x) | TypeAnn({term: Var(x), _}, _), Fun(_) | FunAnn(_)) =>
    VarMap.extend(ctx, (x, {id: pat.id, typ: ty_ann}))
  | _ => ctx
  };

let typ_exp_binop_int: Term.UExp.op_int => Typ.t =
  fun
  | (Plus | Minus | Times | Divide) as _op => Int
  | (LessThan | GreaterThan | Equals) as _op => Bool;

let typ_exp_binop_float: Term.UExp.op_float => Typ.t =
  fun
  | (Plus | Minus | Times | Divide) as _op => Int
  | (LessThan | GreaterThan | Equals) as _op => Bool;

let typ_exp_binop: Term.UExp.op_bin => (Typ.t, Typ.t, Typ.t) =
  fun
  | Bool(And | Or) => (Bool, Bool, Bool)
  | Int(op) => (Int, Int, typ_exp_binop_int(op))
  | Float(op) => (Float, Float, typ_exp_binop_float(op));

let rec uexp_to_info_map =
        (~ctx: Ctx.t, ~mode=Typ.Syn, {id, term}: Term.UExp.t)
        : (Typ.t, Ctx.co, map) => {
  let cls = Term.UExp.cls_of_term(term);
  let go = uexp_to_info_map(~ctx);
  let add = (~self, ~free, m) => (
    typ_after_fix(mode, self),
    free,
    Id.Map.add(id, InfoExp({cls, self, mode, ctx, free}), m),
  );
  let atomic = self => add(~self, ~free=[], Id.Map.empty);
  switch (term) {
  | Invalid(_p) => (Unknown(Internal), [], Id.Map.singleton(id, Invalid))
  | MultiHole(ids, es) =>
    let es = List.map(go(~mode=Syn), es);
    let self = Typ.Multi;
    let free = Ctx.union(List.map(((_, f, _)) => f, es));
    let info: t = InfoExp({cls, self, mode, ctx, free});
    let m = union_m(List.map(((_, _, m)) => m, es));
    let m = List.fold_left((m, id) => Id.Map.add(id, info, m), m, ids);
    (typ_after_fix(mode, self), free, m);
  | EmptyHole => atomic(Just(Unknown(Internal)))
  | Bool(_) => atomic(Just(Bool))
  | Int(_) => atomic(Just(Int))
  | Float(_) => atomic(Just(Float))
  | ListNil => atomic(Just(List(Unknown(Internal))))
  | ListLit(ids, es) =>
    //TODO(andrew) LISTLITS: below is placeholder logic, probably messy/wrong/incomplete
    let modes: list(Typ.mode) =
      switch (mode) {
      | Syn => List.init(List.length(es), _ => Typ.Syn)
      | Ana(ty) =>
        List.init(List.length(es), _ => Typ.Ana(Typ.matched_list(ty)))
      };
    let e_ids = List.map((e: Term.UExp.t) => e.id, es);
    let infos = List.map2((e, mode) => go(~mode, e), es, modes);
    let tys = List.map(((ty, _, _)) => ty, infos);
    let self =
      switch (Typ.join_all(tys)) {
      | None =>
        Typ.Joined(List.map2((id, ty): Typ.source => {id, ty}, e_ids, tys))
      | Some(ty) => Typ.Just(List(ty))
      };
    let free = Ctx.union(List.map(((_, f, _)) => f, infos));
    let info: t = InfoExp({cls, self, mode, ctx, free});
    let m = union_m(List.map(((_, _, m)) => m, infos));
    let m = List.fold_left((m, id) => Id.Map.add(id, info, m), m, ids);
    (typ_after_fix(mode, self), free, m);
  | Var(name) =>
    switch (VarMap.lookup(ctx, name)) {
    | None => atomic(Free)
    | Some(ce) =>
      add(~self=Just(ce.typ), ~free=[(name, [{id, mode}])], Id.Map.empty)
    }
  | BinOp(op, e1, e2) =>
    let (ty1, ty2, ty_out) = typ_exp_binop(op);
    let (_, free1, m1) = go(~mode=Ana(ty1), e1);
    let (_, free2, m2) = go(~mode=Ana(ty2), e2);
    add(
      ~self=Just(ty_out),
      ~free=Ctx.union([free1, free2]),
      union_m([m1, m2]),
    );
  | Cons(e1, e2) =>
    let mode_ele = Typ.matched_list_mode(mode);
    let (ty1, free1, m1) = go(~mode=mode_ele, e1);
    let (_, free2, m2) = go(~mode=Ana(List(ty1)), e2);
    add(
      ~self=Just(List(ty1)),
      ~free=Ctx.union([free1, free2]),
      union_m([m1, m2]),
    );
  | Parens(e) =>
    let (ty, free, m) = go(~mode, e);
    add(~self=Just(ty), ~free, m);
  | Pair(e1, e2) =>
    let (mode_l, mode_r) = Typ.matched_pair_mode(mode);
    let (ty1, free1, m1) = go(~mode=mode_l, e1);
    let (ty2, free2, m2) = go(~mode=mode_r, e2);
    add(
      ~self=Just(Prod(ty1, ty2)),
      ~free=Ctx.union([free1, free2]),
      union_m([m1, m2]),
    );
  | NTuple(ids, es) =>
    //TODO(andrew): N-Tuples. Below is just placeholder logic
    switch (List.rev(es)) {
    | [] => failwith("ERROR: NTuple with no elements")
    | [_] => failwith("ERROR: NTuple with one element")
    | [e1, ...es] =>
      go(
        List.fold_left2(
          (acc, e2, id): Term.UExp.t => {id, term: Term.UExp.Pair(e2, acc)},
          e1,
          es,
          ids,
        ),
      )
    }
  | Test(test) =>
    let (_, free_test, m1) = go(~mode=Ana(Bool), test);
    add(~self=Just(Unit), ~free=free_test, m1);
  | If(cond, e1, e2) =>
    let (_, free_e0, m1) = go(~mode=Ana(Bool), cond);
    let (ty_e1, free_e1, m2) = go(~mode, e1);
    let (ty_e2, free_e2, m3) = go(~mode, e2);
    add(
      ~self=Joined([{id: e1.id, ty: ty_e1}, {id: e2.id, ty: ty_e2}]),
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
      uexp_to_info_map(
        ~ctx,
        ~mode=Ana(Arrow(Unknown(SynSwitch), Unknown(SynSwitch))),
        fn,
      );
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
    let (ty_pat, ctx_pat, m_pat) = upat_to_info_map(~mode=mode_pat, pat);
    let ctx_body = VarMap.union(ctx_pat, ctx);
    let (ty_body, free_body, m_body) =
      uexp_to_info_map(~ctx=ctx_body, ~mode=mode_body, body);
    add(
      ~self=Just(Arrow(ty_pat, ty_body)),
      ~free=Ctx.subtract(ctx_pat, free_body),
      union_m([m_pat, m_body]),
    );
  | FunAnn(pat, typ, body) =>
    let (ty_ann, m_typ) = utyp_to_info_map(typ);
    let (mode_pat, mode_body) =
      switch (mode) {
      | Syn => (Typ.Syn, Typ.Syn)
      | Ana(ty) =>
        let (ty_in, ty_out) = Typ.matched_arrow(ty);
        let ty_in' = Typ.join_or_fst(ty_ann, ty_in);
        (Ana(ty_in'), Ana(ty_out));
      };
    let (ty_pat, ctx_pat, m_pat) = upat_to_info_map(~mode=mode_pat, pat);
    let ctx_body = VarMap.union(ctx_pat, ctx);
    let (ty_body, free_body, m_body) =
      uexp_to_info_map(~ctx=ctx_body, ~mode=mode_body, body);
    add(
      ~self=Just(Arrow(ty_pat, ty_body)),
      ~free=Ctx.subtract(ctx_pat, free_body),
      union_m([m_pat, m_body, m_typ]),
    );
  | Let(pat, def, body) =>
    let (ty_pat, _ctx_pat, _m_pat) = upat_to_info_map(~mode=Syn, pat);
    let def_ctx = extend_let_def_ctx(ctx, pat, def, ty_pat);
    let (ty_def, free_def, m_def) =
      uexp_to_info_map(~ctx=def_ctx, ~mode=Ana(ty_pat), def);
    /* Analyze pattern to incorporate def type into ctx */
    let (_, ctx_pat_ana, m_pat) = upat_to_info_map(~mode=Ana(ty_def), pat);
    let ctx_body = VarMap.union(ctx_pat_ana, def_ctx);
    let (ty_body, free_body, m_body) =
      uexp_to_info_map(~ctx=ctx_body, ~mode, body);
    add(
      ~self=Just(ty_body),
      ~free=Ctx.union([free_def, Ctx.subtract(ctx_pat_ana, free_body)]),
      union_m([m_pat, m_def, m_body]),
    );
  | LetAnn(pat, typ, def, body) =>
    let (ty_ann, m_typ) = utyp_to_info_map(typ);
    let (ty_pat, _ctx_pat, m_pat) =
      upat_to_info_map(~mode=Ana(ty_ann), pat);
    let def_ctx = extend_let_def_ctx(ctx, pat, def, ty_ann);
    let (ty_def, free_def, m_def) =
      uexp_to_info_map(~ctx=def_ctx, ~mode=Ana(ty_pat), def);
    /* Join if pattern and def are consistent, otherwise pattern wins */
    let joint_ty = Typ.join_or_fst(ty_pat, ty_def);
    /* Analyze pattern to incorporate def type into ctx */
    let (_, ctx_pat_ana, _) = upat_to_info_map(~mode=Ana(joint_ty), pat);
    let ctx_body = VarMap.union(ctx_pat_ana, def_ctx);
    let (ty_body, free_body, m_body) =
      uexp_to_info_map(~ctx=ctx_body, ~mode, body);
    add(
      ~self=Just(ty_body),
      ~free=Ctx.union([free_def, Ctx.subtract(ctx_pat_ana, free_body)]),
      union_m([m_pat, m_typ, m_def, m_body]),
    );
  };
}
and upat_to_info_map =
    (~ctx=Ctx.empty, ~mode: Typ.mode=Typ.Syn, {id, term}: Term.UPat.t)
    : (Typ.t, Ctx.t, map) => {
  let cls = Term.UPat.cls_of_term(term);
  let add = (~self, ~ctx, m) => (
    typ_after_fix(mode, self),
    ctx,
    Id.Map.add(id, InfoPat({cls, self, mode, ctx}), m),
  );
  let atomic = self => add(~self, ~ctx, Id.Map.empty);
  switch (term) {
  | Invalid(_) => atomic(Just(Unknown(SynSwitch))) //TODO: ?
  | MultiHole(ids, ps) =>
    let ps = List.map(upat_to_info_map(~ctx, ~mode=Syn), ps);
    let self = Typ.Multi;
    let info: t = InfoPat({cls, self, mode, ctx});
    let m = union_m(List.map(((_, _, m)) => m, ps));
    let m = List.fold_left((m, id) => Id.Map.add(id, info, m), m, ids);
    (typ_after_fix(mode, self), ctx, m);
  | EmptyHole => atomic(Just(Unknown(SynSwitch)))
  | Wild => atomic(Just(Unknown(SynSwitch)))
  | Int(_) => atomic(Just(Int))
  | Float(_) => atomic(Just(Float))
  | Bool(_) => atomic(Just(Bool))
  | ListNil => atomic(Just(List(Unknown(Internal))))
  | Var(name) =>
    let self = Typ.Just(Unknown(SynSwitch));
    let typ = typ_after_fix(mode, self);
    add(~self, ~ctx=VarMap.extend(ctx, (name, {id, typ})), Id.Map.empty);
  | Pair(p1, p2) =>
    let (mode_l, mode_r) = Typ.matched_pair_mode(mode);
    let (ty_p1, ctx, m_p1) = upat_to_info_map(~ctx, ~mode=mode_l, p1);
    let (ty_p2, ctx, m_p2) = upat_to_info_map(~ctx, ~mode=mode_r, p2);
    add(~self=Just(Prod(ty_p1, ty_p2)), ~ctx, union_m([m_p1, m_p2]));
  | Parens(p) =>
    let (ty, ctx, m) = upat_to_info_map(~ctx, ~mode, p);
    add(~self=Just(ty), ~ctx, m);
  | TypeAnn(p, ty) =>
    let (ty_ann, m_typ) = utyp_to_info_map(ty);
    let (_ty, ctx, m) = upat_to_info_map(~ctx, ~mode=Ana(ty_ann), p);
    add(~self=Just(ty_ann), ~ctx, union_m([m, m_typ]));
  };
}
and utyp_to_info_map = ({id, term} as utyp: Term.UTyp.t): (Typ.t, map) => {
  let cls = Term.UTyp.cls_of_term(term);
  let ty = Term.utyp_to_ty(utyp);
  let return = m => (ty, Id.Map.add(id, InfoTyp({cls, ty}), m));
  switch (term) {
  | MultiHole(ids, ts) =>
    let ts = List.map(utyp_to_info_map, ts);
    let ty = Typ.Unknown(Internal);
    let m =
      List.fold_left(
        (m, id) => Id.Map.add(id, InfoTyp({cls, ty}), m),
        union_m(List.map(((_, m)) => m, ts)),
        ids,
      );
    (ty, m);
  | Invalid(_)
  | EmptyHole
  | Unit
  | Int
  | Float
  | Bool
  | ListNil => return(Id.Map.empty)
  | Arrow(t1, t2)
  | Prod(t1, t2) =>
    let (_, m_t1) = utyp_to_info_map(t1);
    let (_, m_t2) = utyp_to_info_map(t2);
    return(union_m([m_t1, m_t2]));
  | Parens(t) =>
    let (_, m) = utyp_to_info_map(t);
    return(m);
  };
};

let mk_map =
  Core_kernel.Memo.general(
    ~cache_size_bound=1000,
    uexp_to_info_map(~ctx=Ctx.empty),
  );
