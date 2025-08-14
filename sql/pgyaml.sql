create extension pgyaml;

select yaml_to_jsonb($$
base: &base
    name: Lyoto Machida
    greeting: こんにちは

foo: &foo
    <<: *base
    age: 10
    fax: 

bar: &bar
    <<: *base
    age: 20
    tel: 86-15150025405
$$::text);

drop extension pgyaml;
