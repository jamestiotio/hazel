(* associator parser definition (we use menhir) *)
(* another approach would be to use, e.g., the shunting yard algorithm,
 *   https://en.wikipedia.org/wiki/Shunting-yard_algorithm
 * but this is easy and fast enough so far *)

%{
  open SemanticsCore
%}

%token <int> PLACEHOLDER
%token COMMA
%token SPACEOP
%token EOF

%left COMMA
%left SPACEOP

%start <SemanticsCore.UHPat.op SemanticsCore.Skel.t> skel_pat

(* %% ends the declarations section of the grammar definition *)

%%

skel_pat: 
  | p = pat; EOF { p }
  ;

pat: 
  | n = PLACEHOLDER { Skel.Placeholder n }
  | p1 = pat; COMMA; p2 = pat {
    Skel.BinOp(
      NotInHole,
      UHPat.Comma,
      p1, p2) }
  | p1 = pat; SPACEOP; p2 = pat { 
    Skel.BinOp(
      NotInHole,
      UHPat.Space, 
      p1, p2) }
  ;

