.PHONY: verify init-golden add-missing-golden ipa

verify:
	python3 scripts/dune_regress.py verify

# First-run only: creates references from the current, visually inspected
# baseline. It refuses to overwrite an existing golden directory.
init-golden:
	python3 scripts/dune_regress.py init-golden

# Adds references for checkpoints that have none (new scenes, after they were
# inspected by eye). Existing references are never overwritten.
add-missing-golden:
	python3 scripts/dune_regress.py add-missing

# Packaging is deliberately downstream of the regression gate.
ipa: verify
	./scripts/build_dune_scummvm_ios.sh
