open Color

let make_rgba32 img =
  match img with
    Images.Index8 i -> Index8.to_rgba32 i
  | Images.Index16 i -> Index16.to_rgba32 i
  | Images.Rgb24 i -> Rgb24.to_rgba32 i
  | Images.Rgba32 i -> i
  | Images.Cmyk32 i -> failwith "CMYK images unsupported"

let string_of_img_format = function
    Images.Gif -> "gif"
  | Images.Bmp -> "bmp"
  | Images.Jpeg -> "jpeg"
  | Images.Tiff -> "tiff"
  | Images.Png -> "png"
  | Images.Xpm -> "xpm"
  | Images.Ppm -> "ppm"
  | Images.Ps -> "ps"

let grey_p r g b =
  abs (r - 128) < 5
  && abs (g - 128) < 5
  && abs (b - 128) < 5

let colour_enc { color = { r = r; g = g; b = b }; alpha = _ } =
  if grey_p r g b then
    None
  else
    let rbit = if r < 128 then 0 else 1
    and gbit = if g < 128 then 0 else 2
    and bbit = if b < 128 then 0 else 4 in
    Some (rbit lor gbit lor bbit)

type byterun =
    Empty of int
  | Lpix_only of int list
  | Rpix_only of int list
  | Solid of int list

(* Mode 2 pixels go:
   L3 R3 L2 R2 L1 R1 L0 R0
*)

let spread pixels =
  (pixels land 1)
  lor ((pixels land 2) lsl 1)
  lor ((pixels land 4) lsl 2)

let mix l r =
  ((spread l) lsl 1) lor (spread r)

let convert_tile img tx ty =
  let pixpairs = ref [] in
  for x = 0 to 7 do
    for y = 0 to 23 do
      let lx = x * 2 in
      let rx = lx + 1 in
      let lpix = Rgba32.get img (tx + lx) (ty + y)
      and rpix = Rgba32.get img (tx + rx) (ty + y) in
      let lbits = colour_enc lpix
      and rbits = colour_enc rpix in
      pixpairs := (lbits, rbits) :: !pixpairs
    done
  done;
  let make_initial = function
    | None, None -> Empty 1
    | Some l, None -> Lpix_only [(spread l) lsl 1]
    | None, Some r -> Rpix_only [spread r]
    | Some l, Some r -> Solid [mix l r] in
  let rec make_byteruns runs_out cur pixpairs =
    match cur, pixpairs with
      _, [] -> cur::runs_out
    | Empty p, (None, None) :: more ->
        make_byteruns runs_out (Empty (succ p)) more
    | Lpix_only p, (Some l, None) :: more ->
        make_byteruns runs_out (Lpix_only (((spread l) lsl 1)::p)) more
    | Rpix_only p, (None, Some r) :: more ->
        make_byteruns runs_out (Rpix_only ((spread r)::p)) more
    | Solid p, (Some l, Some r) :: more ->
        make_byteruns runs_out (Solid ((mix l r) :: p)) more
    | _, item :: more ->
        make_byteruns (cur::runs_out) (make_initial item) more in
  let encoded =
    match !pixpairs with
      [] -> failwith "Empty pixel list?"
    | item::rest -> make_byteruns [] (make_initial item) rest in
  (*let tot = List.fold_right
    (fun ent acc ->
      match ent with
        Empty n ->
          Printf.printf "empty: %d\n" n;
          acc + 1
      | Lpix_only n ->
          Printf.printf "left pix: %s\n"
            (String.concat "," (List.rev_map string_of_int n));
          acc + 1 + (List.length n)
      | Rpix_only n ->
          Printf.printf "right pix: %s\n"
            (String.concat "," (List.rev_map string_of_int n));
          acc + 1 + (List.length n)
      | Solid n ->
          Printf.printf "solid pix: %s\n"
            (String.concat "," (List.rev_map string_of_int n));
          acc + 1 + (List.length n))
    encoded
    0 in
  Printf.printf "total: %d bytes\n" tot;
  tot,*) encoded

let convert_run fo img tx ty runlength =
  for x = 0 to runlength - 1 do
    for y = 0 to 7 do
      let lx = x * 2 in
      let rx = lx + 1 in
      let lpix = Rgba32.get img (tx + lx) (ty + y)
      and rpix = Rgba32.get img (tx + rx) (ty + y) in
      let lbits = colour_enc lpix
      and rbits = colour_enc rpix in
      match lbits, rbits with
        Some l, Some r ->
          Printf.fprintf fo "\t.byte %d\n" (mix l r)
      | _ -> failwith "Too transparent."
    done
  done

let tiles =
  [(* Plain.  *)
   0, 0;
   1, 0;
   2, 0;
   3, 0;
   4, 0;
   5, 0;
   
   (* Vertical stripe.  *)
   4, 6;
   5, 6;
   6, 6;
   7, 6;
   8, 6;
   9, 6;
   
   (* Horizontal stripe.  *)
   4, 7;
   5, 7;
   6, 7;
   7, 7;
   8, 7;
   9, 7;
   
   (* Wrapped.  *)
   0, 4;
   1, 3;
   2, 4;
   3, 3;
   4, 4;
   5, 3;

   (* Cream swirl.  *)
   2, 6;
   
   (* Cage.  *)
   0, 6;
   
   (* Colour bomb.  *)
   5, 5;
   
   (* Explosion.  *)
   7, 3;

   (* Plain BG.  *)
   4, 3;
   
   (* Jelly 1.  *)
   1, 5;
   
   (* Jelly 2.  *)
   3, 5;
   
   (* Hole.  *)
   6, 4]

let write_bytelist fo bl =
  List.iter
    (fun byte -> Printf.fprintf fo "\t.byte %d\n" byte)
    bl

let rec write_span fo = function
    Empty p ->
      Printf.fprintf fo "\t.byte %d\t; empty\n" (p land 63);
      if p >= 64 then
        write_span fo (Empty (p - 64))
  | Solid bl ->
      let len = List.length bl in
      assert (len < 64);
      Printf.fprintf fo "\t.byte %d\t; solid\n" ((len land 63) lor 0x40);
      write_bytelist fo bl
  | Lpix_only bl ->
      let len = List.length bl in
      assert (len < 64);
      Printf.fprintf fo "\t.byte %d\t; lpix\n" ((len land 63) lor 0x80);
      write_bytelist fo bl
  | Rpix_only bl ->
      let len = List.length bl in
      assert (len < 64);
      Printf.fprintf fo "\t.byte %d\t; rpix\n" ((len land 63) lor 0xc0);
      write_bytelist fo bl

let write_block fo num eb =
  Printf.fprintf fo "blk%d:\n" num;
  match eb with
    [Solid eb] when List.length eb = 192 ->
      write_bytelist fo eb
  | _ -> List.iter (fun x -> write_span fo x) eb    

let levels =
  let s x = x lor 128 and c x = x lor 64 in
  [
    [| [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 1; 0; 0; 0; 0 |];
       [| 0; 0; 0; 1; 1; 1; 0; 0; 0 |];
       [| 0; 0; 0; 0; 1; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |] |], 50;

    [| [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 1; 1; 0; 0; 1; 1; 0 |];
       [| 0; 1; 0; 0; 0; 1; 0; 0; 0 |];
       [| 0; 1; 1; 1; 0; 1; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 1; 1; 1; 0; 0; 1; 1; 0 |];
       [| 0; 0; 1; 0; 0; 1; 0; 0; 0 |];
       [| 0; 0; 1; 0; 0; 1; 1; 1; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |] |], 50;

    [| [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 2; 2; 2; 2; 2; 2; 2; 2; 2 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |];
       [| 0; 0; 0; 0; 0; 0; 0; 0; 0 |] |], 30;

    [| [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [| s 0; s 0; s 0; s 0; s 0; s 0; s 0; s 0; s 0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   2;   2;   0;   2;   2;   0;   0 |] |], 40;

    [| [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0; c 0; c 0; c 0;   0;   0;   0 |];
       [|   0;   0;   0; c 0;   2; c 0;   0;   0;   0 |];
       [|   0;   0;   0; c 0; c 0; c 0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |] |], 40;

    [| [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0; s 0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0; s 0;   0;   0;   0;   0 |];
       [| c 2; c 2; c 2; c 2; s 0; c 2; c 2; c 2; c 2 |];
       [| c 2; c 2; c 2; c 2; s 0; c 2; c 2; c 2; c 2 |] |], 100;

    [| [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [| s 0; s 0; s 0; s 0; s 0; s 0; s 0; s 0; s 0 |];
       [| c 0; c 0; c 0; c 0; c 0; c 0; c 0; c 0; c 0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   1;   1;   1;   1;   1;   1;   1;   1;   1 |];
       [|   1;   1;   1;   1;   1;   1;   1;   1;   1 |] |], 100;

    [| [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [|   0;   0;   0;   0;   0;   0;   0;   0;   0 |];
       [| s 0; s 0; s 0; s 0; s 0; s 0; s 0; s 0; s 0 |];
       [|   1;   1; c 0; c 0; c 0; c 0; c 0;   1;   1 |];
       [|   1; c 0;   0;   0;   0;   0;   0; c 0;   1 |];
       [|   1;   1;   0;   0;   0;   0;   0;   1;   1 |] |], 35;
  ]

let write_level fo lev num =
  let ld, moves = lev in
  Printf.fprintf fo "level%d:\n" num;
  Printf.fprintf fo "\t.byte %d\t; moves\n" moves;
  for j = 0 to 8 do
    for i = 0 to 8 do
      let tile = ld.(j).(i) in
      Printf.fprintf fo "\t.byte %d\n" tile
    done
  done

let _ =
  let infile = ref ""
  and outfile = ref "" in
  let argspec =
    ["-o", Arg.Set_string outfile, "Set output file"]
  and usage = "Usage: fontconv infile -o outfile" in
  Arg.parse argspec (fun name -> infile := name) usage;
  if !infile = "" || !outfile = "" then begin
    Arg.usage argspec usage;
    exit 1
  end;
  let img = Images.load !infile [] in
  let xsize, ysize = Images.size img in
  Printf.fprintf stderr "Got image: size %d x %d\n" xsize ysize;
  flush stderr;
  let cinv = make_rgba32 img in
  let enclist = List.fold_right
    (fun (x, y) el ->
      let encoded = convert_tile cinv (16 * x) (24 * y) in
      encoded::el)
    tiles
    [] in
  let fo = open_out !outfile in
  Printf.fprintf fo "\t.segment \"DATA\"\n\t.export tiles\ntiles:\n";
  List.iteri
    (fun i _ -> Printf.fprintf fo "\t.word blk%d\n" i)
    enclist;
  Printf.fprintf fo "\t.word digits\n";
  Printf.fprintf fo "\t.word jelly\n";
  Printf.fprintf fo "\t.word score\n";
  Printf.fprintf fo "\t.word moves\n";
  List.iteri
    (fun i _ ->
      Printf.fprintf fo "\t.word level%d\n" (i + 1))
    levels;
  Printf.fprintf fo "\t.word 0\t; end of levels marker\n";
  List.iteri
    (fun i encblock ->
      write_block fo i encblock)
    enclist;
  Printf.fprintf fo "digits:\n";
  for y = 0 to 2 do
    for x = 0 to 3 do
      convert_run fo cinv (9 * 16 + x * 4) (24 + y * 8) 2
    done
  done;
  Printf.fprintf fo "jelly:\n";
  convert_run fo cinv (9 * 16) (24 * 3) 8;
  Printf.fprintf fo "score:\n";
  convert_run fo cinv (9 * 16) (24 * 3 + 8) 10;
  Printf.fprintf fo "moves:\n";
  convert_run fo cinv (9 * 16) (24 * 3 + 8 * 2) 11;
  List.iteri
    (fun i lev ->
      write_level fo lev (i + 1))
    levels;
  close_out fo
