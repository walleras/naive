-- form.sml -- a form: inputs live in a set that is added to the submit box.
-- Submit gathers only the input widgets in that set (labels are ignored).

CREATE Title AS H2
    WITH text = "Contact form"
    AT X.Center, Y.10

CREATE Intro AS TextBox
    WITH text = "Fill in the fields, then Send. (Labels like this are not submitted.)"
    AT X.Left, Y.44

-- the form set that groups the inputs
CREATE Contact AS SET

CREATE NameF AS TextInput
    WITH placeholder = "Your name", name = "name"
    AT X.Left, Y.74 MEMBER OF Contact

CREATE Msg AS Textarea
    WITH placeholder = "Your message...", name = "message", rows = 4
    AT X.Left, Y.108 MEMBER OF Contact

CREATE Topic AS Select
    WITH name = "topic"
    AT X.Left, Y.212 MEMBER OF Contact
CREATE Ot1 AS Option WITH text = "General enquiry", value = "general" MEMBER OF Topic
CREATE Ot2 AS Option WITH text = "Support",         value = "support" MEMBER OF Topic
CREATE Ot3 AS Option WITH text = "Sales",           value = "sales"   MEMBER OF Topic

CREATE Sub AS Checkbox
    WITH label = "Subscribe to updates", name = "subscribe", value = "yes"
    AT X.Left, Y.252 MEMBER OF Contact

CREATE SendBtn AS Button
    WITH label = "Send", action = "/submit"
    AT X.Left, Y.292

-- the form set must be added to the submit box, or nothing is gathered
ADD Contact TO SendBtn
