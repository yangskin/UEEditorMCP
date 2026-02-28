"""
Action Registry — metadata + search for all UE MCP actions.

The registry maps human-friendly action IDs (e.g. ``blueprint.create``)
to C++ command types (e.g. ``create_blueprint``) and stores schemas,
tags, descriptions, and examples for each action.

No external dependencies — search uses simple keyword matching.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class ActionDef:
    """Immutable definition of a single action."""
    id: str                           # e.g. "blueprint.create"
    command: str                      # C++ command type
    tags: tuple[str, ...]             # search tags
    description: str                  # one-line English description
    input_schema: dict[str, Any]      # JSON Schema
    examples: tuple[dict, ...] = ()   # minimal usage examples
    capabilities: tuple[str, ...] = ("write",)  # "read" | "write" | "destructive"
    risk: str = "safe"                # "safe" | "moderate" | "destructive"

    # ── search helpers ──────────────────────────────────────────────
    _search_text: str = field(default="", repr=False, compare=False)

    def __post_init__(self):
        # Build a single searchable text blob (lowercase)
        parts = [
            self.id.replace(".", " "),
            " ".join(self.tags),
            self.description,
            self.command.replace("_", " "),
        ]
        object.__setattr__(self, "_search_text", " ".join(parts).lower())


class ActionRegistry:
    """Registry of all available actions with keyword search."""

    def __init__(self) -> None:
        self._actions: dict[str, ActionDef] = {}      # id → ActionDef
        self._by_command: dict[str, ActionDef] = {}    # command → ActionDef

    # ── registration ────────────────────────────────────────────────

    def register(self, action: ActionDef) -> None:
        """Register an action definition."""
        if action.id in self._actions:
            raise ValueError(f"Duplicate action id: {action.id}")
        self._actions[action.id] = action
        self._by_command[action.command] = action

    def register_many(self, actions: list[ActionDef]) -> None:
        for a in actions:
            self.register(a)

    # ── lookup ──────────────────────────────────────────────────────

    def get(self, action_id: str) -> ActionDef | None:
        """Get action by id."""
        return self._actions.get(action_id)

    def get_by_command(self, command: str) -> ActionDef | None:
        """Get action by C++ command type."""
        return self._by_command.get(command)

    @property
    def all_ids(self) -> list[str]:
        return sorted(self._actions.keys())

    @property
    def count(self) -> int:
        return len(self._actions)

    # ── search ──────────────────────────────────────────────────────

    def search(
        self,
        query: str,
        tags: list[str] | None = None,
        top_k: int = 10,
    ) -> list[dict[str, Any]]:
        """Search actions by keyword query and optional tag filter.

        Returns a list of dicts: {id, description, tags, capabilities, score}.
        """
        keywords = _tokenize(query)
        if not keywords and not tags:
            # No query — return all actions (limited)
            return [
                _action_summary(a) for a in
                sorted(self._actions.values(), key=lambda a: a.id)[:top_k]
            ]

        tag_set = set(t.lower() for t in tags) if tags else None

        scored: list[tuple[float, ActionDef]] = []
        for action in self._actions.values():
            # Tag filter: if specified, action must have ALL tags
            if tag_set and not tag_set.issubset(set(action.tags)):
                continue

            score = 0.0
            text = action._search_text

            for kw in keywords:
                # Exact word match in id gets highest weight
                if kw in action.id.lower():
                    score += 3.0
                # Tag match
                if kw in action.tags:
                    score += 2.0
                # Substring match in search text
                count = text.count(kw)
                if count > 0:
                    score += count * 1.0

            if score > 0 or tag_set:
                scored.append((score, action))

        # Sort by score descending, then by id
        scored.sort(key=lambda x: (-x[0], x[1].id))

        return [
            {**_action_summary(a), "score": round(s, 1)}
            for s, a in scored[:top_k]
        ]

    # ── schema export ───────────────────────────────────────────────

    def schema(self, action_id: str) -> dict[str, Any] | None:
        """Return full schema + examples for an action."""
        action = self._actions.get(action_id)
        if action is None:
            return None
        return {
            "id": action.id,
            "command": action.command,
            "description": action.description,
            "tags": list(action.tags),
            "input_schema": action.input_schema,
            "examples": list(action.examples),
            "capabilities": list(action.capabilities),
            "risk": action.risk,
        }


# ── helpers ─────────────────────────────────────────────────────────

_SPLIT_RE = re.compile(r"[^a-z0-9]+")


def _tokenize(text: str) -> list[str]:
    """Split text into lowercase keyword tokens."""
    return [t for t in _SPLIT_RE.split(text.lower()) if len(t) >= 2]


def _action_summary(a: ActionDef) -> dict[str, Any]:
    return {
        "id": a.id,
        "description": a.description,
        "tags": list(a.tags),
        "capabilities": list(a.capabilities),
    }


# ── singleton ───────────────────────────────────────────────────────

_registry: ActionRegistry | None = None


def get_registry() -> ActionRegistry:
    """Get or create the global action registry (lazy-loaded)."""
    global _registry
    if _registry is None:
        _registry = ActionRegistry()
        from .actions import register_all_actions
        register_all_actions(_registry)
    return _registry
