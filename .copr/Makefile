# .copr/Makefile — lets Fedora COPR build an SRPM straight from this repo.
#
# COPR project settings: Build source = "Custom", Custom script = the
# default (`make -f .copr/Makefile srpm`), Spec file = crowsnest.spec.
# COPR invokes the target with $outdir set; locally it defaults to .copr-out/.
#
# NOTE: the git-archive prefix intentionally matches the GitHub tag tarball
# layout (`Crowsnest-<version>/`, capital S) so `%autosetup -n` in the spec
# works unchanged. Plain `Version:` parsing avoids needing rpmspec.
SPECFILE := cpp/packaging/rpm/crowsnest.spec
NAME := Crowsnest
VERSION := $(shell grep -E '^Version:' $(SPECFILE) | awk '{print $$2}')
outdir ?= .copr-out

srpm:
	mkdir -p $(outdir)
	git archive --format tar.gz --prefix $(NAME)-$(VERSION)/ \
		-o $(outdir)/crowsnest-$(VERSION).tar.gz HEAD
	cp $(SPECFILE) $(outdir)/
	cd $(outdir) && rpmbuild -bs \
		--define "_sourcedir $(CURDIR)/$(outdir)" \
		--define "_srcrpmdir $(CURDIR)/$(outdir)" \
		$(notdir $(SPECFILE))


