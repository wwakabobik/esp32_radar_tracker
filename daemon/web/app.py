from __future__ import annotations

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from config import WEB_HOST, WEB_PORT
from db import get_display_layout, get_today_work_seconds, set_display_layout
from hub import HubDaemon
from web.routes import dashboard, display, settings


class LayoutItem(BaseModel):
    slot: int
    widget: str


class LayoutUpdate(BaseModel):
    layout: list[LayoutItem]


def create_app(daemon: HubDaemon) -> FastAPI:
    app = FastAPI(title="Presence Hub")
    app.state.daemon = daemon
    app.state.web_host = WEB_HOST
    app.state.web_port = WEB_PORT

    app.include_router(dashboard.router)
    app.include_router(display.router)
    app.include_router(settings.router)

    static_dir = __import__("pathlib").Path(__file__).parent / "static"
    app.mount("/static", StaticFiles(directory=static_dir), name="static")

    @app.get("/", response_class=HTMLResponse)
    async def index() -> str:
        return (static_dir / "index.html").read_text(encoding="utf-8")

    return app
