from pydantic import BaseModel, Field


class Student(BaseModel):
    uuid: str
    student_id_enc: str = Field(..., description="XOR-encrypted hex of student ID")
    forename: str
    surname: str
    passphrase: str
    registered_at: float = Field(..., description="Unix timestamp")


class Meeting(BaseModel):
    uuid: str
    finder_uuid: str
    target_uuid: str
    met_at: float
    answers: str = Field(default="[]", description="JSON array of {question, answer}")


class Announcement(BaseModel):
    uuid: str
    message: str
    sent_at: float
