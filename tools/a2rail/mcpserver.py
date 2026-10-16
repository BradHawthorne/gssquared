"""Source-checkout entry point for the native emulator MCP tool server."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'clients/python/src'))
from gs2debug.mcp import main
if __name__=='__main__':raise SystemExit(main())
