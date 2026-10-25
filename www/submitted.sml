-- www/submitted.sml -- served for route "/submit" after a form submit

CREATE Home AS TextBox
    WITH text = "<- Home", link = "/"
    AT X.Left, Y.Top

CREATE Title AS H2
    WITH text = "Thanks -- form received"
    AT X.Center, Y.40

CREATE Body AS TextBox
    WITH text = "Your inputs were gathered as a set and submitted."
    AT X.Center, Y.Center
