set -e

export PATH="`dirname $0`/../tools":$PATH

for i in $(seq 1 100); do
	SEED=$(echo $RANDOM)
	echo "Seed: $SEED"
	llvm-stress -o slicing-stress-test.ll -seed $SEED
	llvm-slicer -c ret -entry "autogen_SD$SEED" slicing-stress-test.ll
done
