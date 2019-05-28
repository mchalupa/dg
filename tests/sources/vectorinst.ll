define <4 x i32*> @foo(i32 *%arg) {
  %tmp = insertelement <4 x i32*> undef, i32* %arg, i32 0

  %g1 = getelementptr i32, i32* %arg, i32 4
  %tmp4 = insertelement <4 x i32*> %tmp,  i32* %g1, i32 1

  %g2 = getelementptr i32, i32* %g1, i32 4
  %tmp5 = insertelement <4 x i32*> %tmp4, i32* %g2, i32 2

  %g3 = getelementptr i32, i32* %g2, i32 4
  %tmp6 = insertelement <4 x i32*> %tmp5, i32* %g3, i32 3
  ret <4 x i32*> %tmp6
}

define i32 @main() {
  %A = alloca i32, align 4
  %tmp = call <4 x i32*> @foo(i32* %A)
  %tmp1 = extractelement <4 x i32*> %tmp, i32 0
  %tmp2 = extractelement <4 x i32*> %tmp, i32 1
  %tmp3 = extractelement <4 x i32*> %tmp, i32 2
  %tmp4 = extractelement <4 x i32*> %tmp, i32 3
  ret i32 0
}

