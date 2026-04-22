INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_lunch', 20260408, 'lunch', 1, '경양식돈가스');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_lunch', 20260408, 'lunch', 2, '쌀밥');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_lunch', 20260408, 'lunch', 3, '양배추샐러드');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_lunch', 20260408, 'lunch', 4, '크루통스프');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_lunch', 20260408, 'lunch', 5, '오리엔탈펜네파스타');

INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_dinner', 20260408, 'dinner', 1, '뚝배기소불고기');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_dinner', 20260408, 'dinner', 2, '잡곡밥');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_dinner', 20260408, 'dinner', 3, '감자채전');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_dinner', 20260408, 'dinner', 4, '돌나물생채');
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name) VALUES ('20260408_dinner', 20260408, 'dinner', 5, '깍두기');

SELECT * FROM jungle_menu;
SELECT dish_order, dish_name FROM jungle_menu WHERE slot_key = '20260408_lunch';
SELECT dish_order, dish_name FROM jungle_menu WHERE slot_key = '20260408_dinner';
SELECT meal_type, dish_name FROM jungle_menu WHERE dish_name = '깍두기';
