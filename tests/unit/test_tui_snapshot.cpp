
TEST(TuiScreenDiffTest, RelativeCursorMoveAccountsForCursorColumn) {
    screen current(12, 1);
    screen previous(12, 1);
    previous.fast_cell_at(0, 0).character = "a";
    current.fast_cell_at(0, 0).character = "b";
    current.fast_cell_at(5, 0).character = "c";

    const string out = current.to_string(previous);
    // Cursor is at column 1 after "b"; reaching cell 5 needs four columns, not five.
    EXPECT_NE(out.find("\033[4C"), string::npos);
    EXPECT_EQ(out.find("\033[5C"), string::npos);
}
