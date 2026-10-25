-- www/index.sml -- served for route "/"

CREATE Title AS TextBox
    WITH text = "SeSaML Home"
    AT X.Center, Y.Top

CREATE Intro AS TextBox
    WITH text = "This page arrived over a (pretend) network as a set."
    AT X.Center, Y.120

CREATE ToDemo AS TextBox
    WITH text = "Fancy demo: the SeSaML playground", link = "/examples/demo.sml"
    AT X.Center, Y.170

CREATE ToExamples AS TextBox
    WITH text = "Browse all examples", link = "/examples"
    AT X.Center, Y.210

CREATE ToDocs AS TextBox
    WITH text = "Read the docs", link = "/docs.sml"
    AT X.Center, Y.250

CREATE ToAbout AS TextBox
    WITH text = "About this project", link = "/about"
    AT X.Center, Y.290

CREATE Broken AS TextBox
    WITH text = "A deliberately broken link", link = "/nowhere"
    AT X.Center, Y.330

CREATE ToXyz AS TextBox
    WITH text = "Visit xyz.com (a different site on the virtual internet)", link = "xyz.com"
    AT X.Center, Y.370

CREATE ToForm AS TextBox
	With text = "Visit Form Example", link = "/form"
	AT X.Center, Y.410