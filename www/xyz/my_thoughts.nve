CREATE Title AS H2
	WITH text = "My Title"
	AT X.Center, Y.10

CREATE Button_1 AS Button, Blue
	WITH label = "This is a Button"
	AT X.Center, Y.Center

-- runs every time Button_1 is clicked (OnClick + a widget = an event binding)
CREATE my_func AS FUNCTION, OnClick, Button_1
	TAKES btn
	IF btn IS Blue THEN
		move_set(btn, Blue, Red)
		RETURN
	END
	IF btn IS Red THEN
		MOVE btn FROM Red TO Blue
		RETURN
	END
END

CREATE move_set AS FUNCTION
	TAKES Set_1, origin_set, destination_set
	REMOVE Set_1 FROM origin_set
	ADD Set_1 TO destination_set
	RETURN
END
