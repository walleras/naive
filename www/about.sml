-- www/about.sml -- served for route "/about"

CREATE Home AS TextBox
    WITH text = "<- Home", link = "/"
    AT X.Left, Y.Top

CREATE Title AS TextBox
    WITH text = "About SeSaML"
    AT X.Center, Y.Top

CREATE Body AS TextBox
    WITH text = "The web except sets."
    AT X.Center, Y.Center

CREATE ToDocs AS TextBox
    WITH text = "Back to docs", link = "/docs"
    AT X.Center, Y.300
