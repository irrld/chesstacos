#include "game_canvas.h"
#include "message_canvas.h"

GameCanvas::GameCanvas(gApp* root, Ref<ChessConnection> connection, Ref<std::thread> thread) : gBaseCanvas(root), connection_(connection), thread_(thread) {
  this->root_ = root;
}

GameCanvas::~GameCanvas() {
  connection_->Close();
  if (thread_ && thread_->joinable()) {
    thread_->join();
  }
}

void GameCanvas::setup() {
  if (!connection_->IsHost()) {
    player_color_ = kPieceColorBlack;
    flip_board_ = true;
  } else {
    player_color_ = kPieceColorWhite;
    flip_board_ = false;
  }
  cursor_.loadImage("cursor.png");
  cursor_.setFiltering(2, 2);
  cursor_height_ = cursor_.getHeight();
  cursor_width_ = 16;
  board_textures_[0].loadImage("boards/board_persp_01.png");
  board_textures_[0].setFiltering(2, 2);
  board_textures_[1].loadImage("boards/board_persp_01_flipped.png");
  board_textures_[1].setFiltering(2, 2);

  board_width_ = board_textures_[0].getWidth() * RenderUtil::kPixelScale;
  board_height_ = board_textures_[0].getHeight() * RenderUtil::kPixelScale;
  board_pos_x = (getWidth() - board_width_) / 2;
  board_pos_y = (getHeight() - board_height_) / 2;
  white_pieces_.loadImage("pieces/WhitePieces-Sheet.png");
  white_pieces_.setFiltering(2, 2);
  black_pieces_.loadImage("pieces/BlackPieces-Sheet.png");
  black_pieces_.setFiltering(2, 2);
  outliner_.loadImage("outliner.png");
  outliner_.setFiltering(2, 2);
  ResetSelection();
  font_.loadFont("PixeloidMono.ttf", 12, false);
  piece_offset_ = white_pieces_.getHeight() - 11;
  input_lock_ = true;
  game_started_ = false;
  ended_ = false;
  // todo move chess board spaghetti stuff to connection and handle everything there
  chess_board_ = CreateRef<ChessBoard>(*connection_->GetBoardData());
  connection_->SetOnMove([this](int x, int y, int to_x, int to_y) {
    Move(x, y, to_x, to_y);
  });
  connection_->SetOnTurnChange([this](PieceColor color) {
    if (color == kPieceColorNone) {
      return;
    }
    auto state = chess_board_->CheckState(color);
    if (state == ChessState::Checkmate) {
      connection_->EndGame(color);
      return;
    } else if (state == ChessState::Stalemate) {
      connection_->EndGame(kPieceColorNone);
      return;
    }
    if (color == player_color_) {
      SetInfoText("Your turn!");
    } else {
      SetInfoText("Opponent's turn!");
    }
  });
  connection_->SetOnStartGame([this]() {
    game_started_ = true;
  });
  connection_->SetOnEndGame([this](PieceColor color) {
    input_lock_ = true;
    ended_ = true;
    winner_color_ = color;
    if (color == kPieceColorNone) {
      SetInfoText("Stalemate!");
    } else if (color == player_color_) {
      SetInfoText("You won!");
    } else {
      SetInfoText("You lost!");
    }
  });
  appmanager->getWindow()->setCursorMode(0x00034002); // GLFW_CURSOR_HIDDEN
  appmanager->setTargetFramerate(120);

  connection_->Ready();
}


void GameCanvas::update() {
  game_time_ += appmanager->getElapsedTime();
  UpdateInfoText();
  if (ended_) {
    dance_anim_time_ += appmanager->getElapsedTime();
    return;
  }
  if (game_started_) {
    UpdateFallAnimation();
  }
}

void GameCanvas::UpdateInfoText() {
  if (has_next_text_) {
    info_text_ = next_info_text_;
    info_pos_x_ = getWidth() / 2;
    info_pos_y_ = board_pos_y + board_height_ + 50;
    has_next_text_ = false;
  }
}

void GameCanvas::UpdateFallAnimation() {
  if (fall_anim_dist_ > 0.0f) {
    fall_anim_dist_ = Lerp(fall_anim_dist_, 0.0f, 0.01f + (game_time_ / 20.0f));
    if (fall_anim_dist_ < 0.001f) {
      fall_anim_dist_ = 0.0f;
      input_lock_ = false;
    }
  }
}

void GameCanvas::draw() {
  clearColor(0x6f, 0x75, 0x83);
  if (!game_started_) {
    return;
  }
  DrawBoard();
  DrawInfoText();
  // pos * 2 -> pos * 4 / 2 -> pos * pixelScale / 2
  cursor_.drawSub(cursor_pos_x_ - cursor_width_ * 2, cursor_pos_y_ - cursor_height_,
                  cursor_width_ * 4, cursor_height_ * 4,
                  cursor_type_ * 16, 0,
                  cursor_width_, cursor_height_);
  /*for (int x = 0; x < 8; ++x) {
    for (int y = 0; y < 8; ++y) {
      RenderUtil::DrawFont(gToStr(x) + " " + gToStr(y),
                    board_pos_x + (7 + x * 16) * RenderUtil::kPixelScale,
                    board_pos_y + (10 + y * 12) * RenderUtil::kPixelScale, {1.0f, 0, 0});
    }
  }*/
}

void GameCanvas::HighlightPosition(int board_x, int board_y, const gColor& color) {
  int render_offset_x = (7 + board_x * 16) * RenderUtil::kPixelScale;
  int render_offset_y = (6 + ConvertY(board_y) * 12) * RenderUtil::kPixelScale;
  const gColor& og_color = renderer->getColor();
  renderer->setColor(color);
  outliner_.draw(board_pos_x + render_offset_x, board_pos_y + render_offset_y, 16 * RenderUtil::kPixelScale, 12 * RenderUtil::kPixelScale);
  renderer->setColor(og_color);
}

bool GameCanvas::Move(int x, int y, int to_x, int to_y) {
  animate_move_piece_x_ = to_x;
  animate_move_piece_y_ = to_y;
  animate_from_pos_x_ = (7 + x * 16) * RenderUtil::kPixelScale;
  animate_from_pos_y_ = (6 + ConvertY(y) * 12 - piece_offset_) * RenderUtil::kPixelScale;
  animate_to_pos_x_ = (7 + to_x * 16) * RenderUtil::kPixelScale;
  animate_to_pos_y_ = (6 + ConvertY(to_y) * 12 - piece_offset_) * RenderUtil::kPixelScale;
  animate_ = true;

  Ref<Piece> piece = chess_board_->GetPiece(x, y);
  chess_board_->SetPiece(to_x, to_y, piece);
  chess_board_->SetPiece(x, y, nullptr);

  if (piece->GetType() == kPieceTypePawn) {
    if (to_y == 0 && piece->GetColor() == kPieceColorBlack) {
      // black promote
      // promote after switch
      if (player_color_ == kPieceColorBlack) {
        SetInfoText("Promote your pawn!");
        promoting_ = true;
        show_moves_ = false;
        hover_piece_x_ = to_x;
        hover_piece_y_ = to_y;
        return false;
      } else {
        SetInfoText("Your opponent is promoting its pawn!");
        return false;
      }
    } else if (to_y == 7 && piece->GetColor() == kPieceColorWhite) {
      // white promote
      // promote after switch
      if (player_color_ == kPieceColorWhite) {
        SetInfoText("Promote your pawn!");
        promoting_ = true;
        show_moves_ = false;
        hover_piece_x_ = to_x;
        hover_piece_y_ = to_y;
        return false;
      } else {
        SetInfoText("Your opponent is promoting its pawn!");
        return false;
      }
    } else if (to_x == x && std::abs(to_y - y) == 2) {
      int jump_y = to_y - y;
      if (jump_y > 0) {
        jump_y -= 1;
      } else {
        jump_y -= -1;
      }
      Ref<Piece> jump_piece = chess_board_->GetPiece(x, y + jump_y);
      if (jump_piece) {
        chess_board_->SetPiece(x, y + jump_y, nullptr);
      }
    }
  }
  return true;
}

void GameCanvas::DrawBoard() {
  renderer->setColor(255, 255, 255);
  board_textures_[flip_board_].draw(board_pos_x, board_pos_y, board_width_,
                                   board_height_);
  if (flip_board_) {
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        DrawPiece(x, y);
      }
    }
  } else {
    for (int x = 0; x < 8; x++) {
      for (int y = 7; y >= 0; y--) {
        DrawPiece(x, y);
      }
    }
  }
}

void GameCanvas::DrawInfoText() {
  if (!info_text_.empty()) {
    RenderUtil::DrawFont(info_text_, info_pos_x_, info_pos_y_, {1.0f, 1.0f, 1.0f}, true);
  }
}

void GameCanvas::DrawPiece(int x, int y) {
  Ref<Piece> piece = chess_board_->GetPiece(x, y);
  if (hover_piece_x_ == x && hover_piece_y_ == y) {
    if (!show_moves_) {
      HighlightPosition(x, y, {0.0f, 139 / 255.0f, 203 / 255.0f});
    }
  }
  if (show_moves_) {
    if (move_piece_x_ == x && move_piece_y_ == y) {
      HighlightPosition(x, y, {250 / 255.0f, 100 / 255.0f, 20 / 255.0f});
    }
    Ref<Piece> hover_piece =
        chess_board_->GetPiece(move_piece_x_, move_piece_y_);
    if (hover_piece) {
      if (chess_board_->IsValidMove(move_piece_x_, move_piece_y_, x, y)) {
        if (hover_piece_x_ == x && hover_piece_y_ == y && hover_piece->GetColor() == connection_->GetCurrentTurn()) {
          HighlightPosition(x, y, {0.3f, 0 / 255.0f, 203 / 255.0f});
        } else {
          HighlightPosition(x, y, {0.0f, 139 / 255.0f, 203 / 255.0f});
        }
      }
    }
  }

  if (!piece) {
    return;
  }
  auto& image = piece->GetColor() == kPieceColorWhite ? white_pieces_ : black_pieces_;
  int index = piece->GetType() - 1;
  if (index < 0) {
    return;
  }
  int render_offset_x = (7 + x * 16) * RenderUtil::kPixelScale;
  int render_offset_y = (6 + ConvertY(y) * 12 - piece_offset_) * RenderUtil::kPixelScale;
  if (animate_ && animate_move_piece_x_ == x && animate_move_piece_y_ == y) {
    image.drawSub(board_pos_x + animate_from_pos_x_, board_pos_y + animate_from_pos_y_, // render pos
                  16 * RenderUtil::kPixelScale, 32 * RenderUtil::kPixelScale, // render size
                  index * 16, index * 32, // image pos
                  16, 32 // image size
    );
    animate_from_pos_x_ = Lerp(animate_from_pos_x_, animate_to_pos_x_, 0.15f);
    animate_from_pos_y_ = Lerp(animate_from_pos_y_, animate_to_pos_y_, 0.15f);
    if (std::abs(animate_to_pos_x_ - animate_from_pos_x_) < 0.02f && std::abs(animate_to_pos_y_ - animate_from_pos_y_) < 0.02f) {
      animate_ = false;
    }
  } else if (ended_ && (piece->GetColor() == winner_color_ || winner_color_ == kPieceColorNone)) {
    double stretch = std::sin(dance_anim_time_ * 6) * 30;
    double compress = (std::cos(dance_anim_time_ * 6)) * 5;
    image.drawSub(board_pos_x + render_offset_x + (compress / 2), board_pos_y + render_offset_y + stretch, // render pos
                  16 * RenderUtil::kPixelScale - compress, 32 * RenderUtil::kPixelScale - stretch, // render size
                  index * 16, index * 32, // image pos
                  16, 32, // image size
                  8 * RenderUtil::kPixelScale - compress, 32 * RenderUtil::kPixelScale - stretch - 3 * RenderUtil::kPixelScale, // pivot center
                  (int) gRadToDeg(std::cos(dance_anim_time_ * 3) * 0.25f) // rotation
    );
  } else {
    int start_offset = -fall_anim_dist_ * 800 * (x + 1);
    image.drawSub(board_pos_x + render_offset_x, board_pos_y + render_offset_y + start_offset, // render pos
                  16 * RenderUtil::kPixelScale, 32 * RenderUtil::kPixelScale, // render size
                  index * 16, index * 32, // image pos
                  16, 32 // image size
    );
  }
}

int GameCanvas::ConvertY(int y) {
  return flip_board_ ? y : 7 - y;
}

void GameCanvas::ResetSelection() {
  show_moves_ = false;
  hover_piece_x_ = -1;
  hover_piece_y_ = -1;
}

void GameCanvas::keyPressed(int key) {
//	gLogi("GameCanvas") << "keyPressed:" << key;
}

void GameCanvas::keyReleased(int key) {
//	gLogi("GameCanvas") << "keyReleased:" << key;
}

void GameCanvas::charPressed(unsigned int codepoint) {
//	gLogi("GameCanvas") << "charPressed:" << gCodepointToStr(codepoint);
}

void GameCanvas::mouseMoved(int x, int y) {
  cursor_pos_x_ = x;
  cursor_pos_y_ = y;
  if (input_lock_ || promoting_ || connection_->GetCurrentTurn() != player_color_) {
    return;
  }
//	gLogi("GameCanvas") << "mouseMoved" << ", x:" << x << ", y:" << y;
  int board_x = (x - board_pos_x) / RenderUtil::kPixelScale - 7;
  int board_y = (y - board_pos_y) / RenderUtil::kPixelScale - 6;
  int piece_x = board_x / 16;
  int piece_y = ConvertY(board_y / 12);
  if (piece_x < 0 || piece_x >= 8 || piece_y < 0 || piece_y >= 8) {
    ResetSelection();
    return;
  }
  hover_piece_x_ = piece_x;
  hover_piece_y_ = piece_y;
  Ref<Piece> piece = chess_board_->GetPiece(hover_piece_x_, hover_piece_y_);
  if (!show_moves_ && piece != nullptr && piece->GetColor() != player_color_) {
    ResetSelection();
  }
}

void GameCanvas::mouseDragged(int x, int y, int button) {
//	gLogi("GameCanvas") << "mouseDragged" << ", x:" << x << ", y:" << y << ", b:" << button;
  cursor_pos_x_ = x;
  cursor_pos_y_ = y;
  cursor_type_ = CursorType::kHandClosed;
}

void GameCanvas::mousePressed(int x, int y, int button) {
//	gLogi("GameCanvas") << "mousePressed" << ", x:" << x << ", y:" << y << ", b:" << button;
  cursor_type_ = CursorType::kHandClosed;
}

void GameCanvas::mouseReleased(int x, int y, int button) {
  cursor_type_ = CursorType::kArrow;
  if (animate_ || input_lock_ || connection_->GetCurrentTurn() != player_color_) {
    return;
  }
  if (promoting_) {
    return;
  }

  int board_x = (x - board_pos_x) / RenderUtil::kPixelScale - 7;
  int board_y = (y - board_pos_y) / RenderUtil::kPixelScale - 6;
  int piece_x = board_x / 16;
  int piece_y = ConvertY(board_y / 12);
  if (piece_x < 0 || piece_x >= 8 || piece_y < 0 || piece_y >= 8) {
    ResetSelection();
    return;
  }
  hover_piece_x_ = piece_x;
  hover_piece_y_ = piece_y;

  bool was_show_moves = show_moves_;
  show_moves_ = false;
  Ref<Piece> piece = chess_board_->GetPiece(hover_piece_x_, hover_piece_y_);
  if (!was_show_moves && piece && piece->GetColor() == player_color_) {
    show_moves_ = true;
    move_piece_x_ = hover_piece_x_;
    move_piece_y_ = hover_piece_y_;
  } else if (was_show_moves && (piece == nullptr || piece->GetColor() != player_color_)) {
    if (animate_ || input_lock_ || !chess_board_->IsValidMove(move_piece_x_, move_piece_y_, hover_piece_x_,
                                  hover_piece_y_)) {
      ResetSelection();
      return;
    }
    bool switch_turn = Move(move_piece_x_, move_piece_y_, hover_piece_x_, hover_piece_y_);
    connection_->Move(move_piece_x_, move_piece_y_, hover_piece_x_,
                      hover_piece_y_, switch_turn);

    ResetSelection();
  }
}

void GameCanvas::SetInfoText(const std::string& text) {
  next_info_text_ = text;
  has_next_text_ = true;
}

void GameCanvas::mouseScrolled(int x, int y) {

}

void GameCanvas::mouseEntered() {

}

void GameCanvas::mouseExited() {

}

void GameCanvas::windowResized(int w, int h) {

}

void GameCanvas::showNotify() {

}

void GameCanvas::hideNotify() {
  connection_->Close();
}
