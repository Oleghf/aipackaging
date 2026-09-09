#include <gtest/gtest.h>
/*
#include <figure.h>
#include <processor.h>
#include <board.h>
#include <iostream>



// БАЗОВЫЕ СЛУЧАИ

// Создание клетки в форме квадрата
TEST(Cell, Rectangle)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    std::string expected("((LINE ((0 0) (1 0) 0 1) (LINE ((1 0) (0 1) 0 1) (LINE ((1 1) (1 0) -1 0) (LINE ((0 1) (0 1) -1 0))");
    std::string actual(cell.to_aipon());

    EXPECT_EQ(expected, actual);
}

// Создание пустой клетки
TEST(Cell, Empty)
{
    Cell cell(0);
    std::string expected("()");
    std::string actual(cell.to_aipon());

    EXPECT_EQ(expected, actual);
}

// Создание фигуры в форме буквы T
TEST(Figure, TShaped)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D(0, 0));
    f.add_cell(cell, Point2D(1, 0));
    f.add_cell(cell, Point2D(2, 0));
    f.add_cell(cell, Point2D(1, 1));

    std::string expected("(#C0 (0 0) #C0 (1 0) #C0 (2 0) #C0 (1 1))");
    std::string actual(f.to_aipon());

    EXPECT_EQ(expected, actual);
}

// Создание пустой фигуры
TEST(Figure, Empty)
{
    Figure f;

    std::string expected("()");
    std::string actual(f.to_aipon());

    EXPECT_EQ(expected, actual);
}

// Десериализация описания клетки в виде квадрата
TEST(Processor, DeserializeCell)
{
    Processor p;

    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    std::string str("CREATE_CELL " + cell.to_aipon() + " -> #C0");
    p.deserialize(str);

    std::string expected("((LINE ((0 0) (1 0) 0 1) (LINE ((1 0) (0 1) 0 1) (LINE ((1 1) (1 0) -1 0) (LINE ((0 1) (0 1) -1 0))");
    std::string actual(p.get_cell(cell.get_id()).to_aipon());

    EXPECT_EQ(expected, actual);
}

// Десериализация описания пустой клетки
TEST(Processor, DeserializeEmptyCell)
{
    Processor p;

    Cell cell(0);

    std::string str(cell.to_aipon());
    p.deserialize("CREATE_CELL " + str + " -> #C0");

    std::string expected("()");
    std::string actual(p.get_cell(cell.get_id()).to_aipon());

    EXPECT_EQ(expected, actual);
}

// Десериализация описания фигуры в форме буквы T
TEST(Processor, DeserializeFigure)
{
    Processor p;

    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    p.deserialize(cell.to_aipon());

    Figure f;
    f.add_cell(cell, Point2D(0, 0));
    f.add_cell(cell, Point2D(1, 0));
    f.add_cell(cell, Point2D(2, 0));
    f.add_cell(cell, Point2D(1, 1));

    p.deserialize("CREATE_FIGURE " + f.to_aipon() + " -> #F0");

    std::string expected(f.to_aipon());
    std::string actual(p.get_figure(0).to_aipon());

    EXPECT_EQ(expected, actual);
}

// Десериализация описания пустой фигуры
TEST(Processor, DeserializeEmptyFigure)
{
    Processor p;

    Figure f;

    p.deserialize("CREATE_FIGURE " + f.to_aipon() + " -> #F0");

    std::string expected(f.to_aipon());
    std::string actual(p.get_figure(0).to_aipon());

    EXPECT_EQ(expected, actual);
}
//получить верхнюю клетку фигуры
TEST(Get_top_up_cell, CommonCase)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D(1, 1));
    f.add_cell(cell, Point2D(1, 2));
    f.add_cell(cell, Point2D(2, 1));

    Point2D expected{ 1, 2 };

    Point2D actual = f.top_up_cell();

    EXPECT_EQ(expected.x, actual.x);
    EXPECT_EQ(expected.y, actual.y);
}
//получить левую клетку фигуры когда ей отрицательные координаты
TEST(Get_top_up_cell, WithNegativeCoords)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D(-1, -1));
    f.add_cell(cell, Point2D(0, 0));
    f.add_cell(cell, Point2D(-2, 3));

    Point2D expected{ -2, 3 };

    Point2D actual = f.top_up_cell();

    EXPECT_EQ(expected.x, actual.x);
    EXPECT_EQ(expected.y, actual.y);
}

// получить верхнюю клетку фигуры когда всего одна клетка
TEST(Get_top_up_cell, SingleCell)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D{ 5,5 });
    Point2D expected{ 5, 5 };

    Point2D actual = f.top_up_cell();

    EXPECT_EQ(expected.x, actual.x);
    EXPECT_EQ(expected.y, actual.y);
}
// получить левую клетку фигуры
TEST(Get_top_left_cell, CommonCase)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D{ 3,3 });
    f.add_cell(cell, Point2D{ 2,4 });
    f.add_cell(cell, Point2D{ 4,1 });
    f.add_cell(cell, Point2D{ 1,3 });

    Point2D expected{ 1, 3 };
    Point2D actual = f.top_left_cell();

    EXPECT_EQ(expected.x, actual.x);
    EXPECT_EQ(expected.y, actual.y);
}
// получить левую клетку фигуры когда ей отрицательные координаты
TEST(Get_top_left_cell, WithNegativeCoords)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D(-1, -1));
    f.add_cell(cell, Point2D(0, 0));
    f.add_cell(cell, Point2D(-2, -3));
    f.add_cell(cell, Point2D(-2, -1));

    Point2D expected{ -2, -3 };
    Point2D actual = f.top_left_cell();

    EXPECT_EQ(expected.x, actual.x);
    EXPECT_EQ(expected.y, actual.y);
}

// получить левую клетку фигуры когда всего одна клетка
TEST(Get_top_left_cell, SingleCell)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Cell cell(0);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D{ 5,5 });

    Point2D expected{ 5, 5 };

    Point2D actual = f.top_left_cell();

    EXPECT_EQ(expected.x, actual.x);
    EXPECT_EQ(expected.y, actual.y);
}

//проверка на выход фигуры за пределы доски
TEST(is_figure_out_of_bounds, Common)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Board board(10, 10);
    Cell cell(1);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D{ 1,1 });
    f.add_cell(cell, Point2D{ 1,2 });
    f.add_cell(cell, Point2D{ 1,3 });
    f.add_cell(cell, Point2D{ 2,1 });

    bool expected = false;
    bool actual = is_figure_out_of_bounds(f,board.get_height(), board.get_width());

    ASSERT_EQ(expected, actual);
}

//проверка на выход фигуры с отрицательными координатами за пределы доски
TEST(is_figure_out_of_bounds, Negative_coords)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Board board(10, 10);
    Cell cell(1);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D{ -1,1 });
    f.add_cell(cell, Point2D{ 11,2 });
    f.add_cell(cell, Point2D{ 0,3 });
    f.add_cell(cell, Point2D{ 2,1 });

    bool expected = true;
    bool actual = is_figure_out_of_bounds(f, board.get_height(), board.get_width());
    ASSERT_EQ(expected, actual);
}
// строка с информацией о доске
TEST(Board_get_string, Common)
{
    Board board(10, 10);

    std::string expected = "CREATE_BOARD (10,10)\n";
    std::string actual = get_string(board);

    ASSERT_EQ(expected, actual);
}

//
TEST(does_figure_collide, one_figure)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Board board(10, 10);

    Cell cell(1);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D{ 1,1 });
    f.add_cell(cell, Point2D{ 1,2 });
    f.add_cell(cell, Point2D{ 1,3 });
    f.add_cell(cell, Point2D{ 2,1 });

    bool expected = false;
    bool actual = does_figure_collide(f,board.get_cells());

    ASSERT_EQ(expected, actual);
}
//проверка столкновение двух фигур
TEST(does_figure_collide, two_figures)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Board board(10, 10);

    Cell cell(1);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Cell cell2(2);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));

    Figure f;
    f.add_cell(cell, Point2D{ 1,1 });
    f.add_cell(cell, Point2D{ 1,2 });
    f.add_cell(cell, Point2D{ 1,3 });
    f.add_cell(cell, Point2D{ 2,1 });

    board.add_figure(std::make_shared<Figure>(f));

    Figure f1(1);
    f1.add_cell(cell2, { 1,1 });
    bool expected = true;
    bool actual =does_figure_collide(f1,board.get_cells());

    ASSERT_EQ(expected, actual);
}

//замена клеток доски на клетки фигуры
TEST(replace_cell, Common)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Board board(10, 10);

    Cell cell(1);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));


    Figure f;
    f.add_cell(cell, Point2D{ 1,1 });
    f.add_cell(cell, Point2D{ 1,2 });
    f.add_cell(cell, Point2D{ 1,3 });
    f.add_cell(cell, Point2D{ 2,1 });

    replace_cell(f,board.get_cells());
    int expected = 1;
    int actual = board.get_cells()[0,0].get_id();

    ASSERT_EQ(expected, actual);
}

//добавление фигуры на доску
TEST(add_figure, Common)
{
    Line2D l1({ 0,0 }, Vector2D{ 1,0 });
    Line2D l2({ 1,0 }, Vector2D{ 0,1 });
    Line2D l3({ 1,1 }, Vector2D{ 1,0 });
    Line2D l4({ 0,1 }, Vector2D{ 0,1 });

    Board board(10, 10);

    Cell cell(1);
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l1), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l2), 0, 1));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l3), -1, 0));
    cell.add_line(std::make_shared<BoundedCurve2D>(std::make_shared<Line2D>(l4), -1, 0));


    Figure f;
    f.add_cell(cell, Point2D{ 2,2 });
    f.add_cell(cell, Point2D{ 2,3 });


    board.add_figure(std::make_shared<Figure>(f));

    int expected = 1;
    int actual1 = board.get_cells()[0, 0].get_id();
    int actual2 = board.get_cells()[0, 1].get_id();
    ASSERT_EQ(expected, actual1);
    ASSERT_EQ(expected,actual2 );
}

*/
