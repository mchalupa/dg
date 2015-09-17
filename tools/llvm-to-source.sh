BCFILE="$1"
NAME="${BCFILE%.*}"
FILE="$NAME.c"
HTMLFILE="$NAME.c.html"
source-highlight -n $FILE

N=1
cat "$HTMLFILE" | grep '<font .*[0-9]\+:' |
while read LINE; do
	# FIXME not very efficient
	if ./llvm-to-source "$BCFILE" | grep -q "$N$"; then
		echo '<span style="background-color:#ddffdd">'"$LINE"'</span>'
	else
		echo "$LINE"
	fi

	N=$(($N + 1))
done
